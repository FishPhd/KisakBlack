"""Only declared original ranges and memory may execute or be accessed."""
from __future__ import annotations

import struct
import time
from .core import Blocked, check_mapping

STACK = 0x70000000
STACK_SIZE = 0x10000
SP = STACK + 0x8000
DATA = 0x71000000
RETURN = 0x72000000


def x87_double_words(mantissa, exponent):
    """Read a finite x87 value already rounded by PC=53, without host FP math."""
    sign = (exponent >> 15) << 63
    if not mantissa:
        if exponent & 0x7fff:
            raise Blocked("Unsupported nonzero x87 exponent with zero mantissa")
        bits = sign
    else:
        biased = (exponent & 0x7fff) - 16383 + 1023
        if not 1 <= biased <= 2046 or not mantissa & (1 << 63) or mantissa & 0x7ff:
            raise Blocked("Unsupported x87 result precision/range; no output rounding is synthesized")
        bits = sign | (biased << 52) | ((mantissa >> 11) & ((1 << 52) - 1))
    return [bits & 0xffffffff, bits >> 32, 0, 0]


class Emulator:
    def __init__(self, f, pe):
        import unicorn as uc
        import unicorn.x86_const as x
        check_mapping(f, pe)
        self.f, self.pe, self.uc, self.x = f, pe, uc, x
        self.base = pe.OPTIONAL_HEADER.ImageBase
        self.machine = uc.Uc(uc.UC_ARCH_X86, uc.UC_MODE_32)
        self.ranges = [(self.base + r["rva"], self.base + r["rva"] + r["size"]) for r in f["code"]]
        self.constants = [(self.base + r["rva"], self.base + r["rva"] + r["size"]) for r in f.get("constants", []) + f.get("globals", [])]
        pages = set()
        for start, end in self.ranges + self.constants:
            pages.update(range(start & ~4095, (end + 4095) & ~4095, 4096))
        for page in sorted(pages):
            self.machine.mem_map(page, 4096)
            self.machine.mem_write(page, pe.get_data(page - self.base, 4096).ljust(4096, b"\0"))
            self.machine.mem_protect(page, 4096, uc.UC_PROT_READ | uc.UC_PROT_EXEC)
        self.machine.mem_map(STACK, STACK_SIZE, uc.UC_PROT_READ | uc.UC_PROT_WRITE)
        self.machine.mem_map(DATA, 0x10000, uc.UC_PROT_READ | uc.UC_PROT_WRITE)
        self.machine.mem_map(RETURN, 4096, uc.UC_PROT_READ | uc.UC_PROT_EXEC)
        self.machine.hook_add(uc.UC_HOOK_CODE, self._code)
        self.machine.hook_add(uc.UC_HOOK_MEM_READ | uc.UC_HOOK_MEM_WRITE, self._memory)
        self.start_context = self.machine.context_save()
        self.error = None

    def _fail(self, message):
        self.error = message
        self.machine.emu_stop()

    def _code(self, machine, address, size, _):
        self.instructions += 1
        if self.instructions % 128 == 0 and time.monotonic() > self.deadline:
            self._fail("Execution time limit reached")
            return
        if not any(start <= address and address + size <= end for start, end in self.ranges):
            self._fail(f"Undeclared instruction at {address:#x}")

    def _memory(self, machine, access, address, size, value, _):
        write = access == self.uc.UC_MEM_WRITE
        allowed = [(SP - 0x1000, SP + 0x100)]
        if write:
            allowed.append((DATA + 0x100, DATA + 0x100 + self.f["output_words"] * 4))
            if self.f["name"] == "FX_SetupVisualState":
                allowed.append((DATA + 0x100, DATA + 0x11c))
            if self.f["name"] == "ExpandBoundsToWidth":
                allowed += [(DATA + 0x108, DATA + 0x10c), (DATA + 0x114, DATA + 0x118)]
            if self.f["name"] == "FX_GetVelocityAtTime":
                allowed.append((DATA + 0x100, DATA + 0x10c))
            if self.f["name"] == "PM_Accelerate":
                allowed.append((DATA + 0x1030, DATA + 0x103C))
        else:
            allowed += self.constants + [(DATA, DATA + 16), (DATA + 0x100, DATA + 0x110)]
            if self.f["name"] == "FX_SetupVisualState":
                allowed.append((DATA + 0x1000, DATA + 0x1124))
            if self.f["name"] == "ExpandBoundsToWidth":
                allowed.append((DATA + 0x100, DATA + 0x118))
            if self.f["name"] == "FX_GetVelocityAtTime":
                allowed += [(DATA + 0x1000, DATA + 0x1124), (DATA + 0x1200, DATA + 0x1230),
                            (DATA + 0x1300, DATA + 0x130c), (DATA + 0x2000, DATA + 0x8060)]
            if self.f["name"] == "PM_Accelerate":
                allowed += [(DATA + 0x1000, DATA + 0x1000 + 0x26E8),
                            (DATA + 0x200, DATA + 0x200 + 0x8C), (DATA + 0x300, DATA + 0x320)]
            if self.f["name"] == "BG_GetQuickScopeSpread":
                allowed += [(DATA + 0x14ec, DATA + 0x14f4), (DATA + 0x1560, DATA + 0x1564)]
                allowed += [(DATA + 0x400 + i * 0x100 + 0x18,
                             DATA + 0x400 + i * 0x100 + 0x18 + (1 if i == 0 else 4)) for i in range(4)]
        if not any(start <= address and address + size <= end for start, end in allowed):
            self._fail(f"Undeclared {'write' if write else 'read'} {address:#x}+{size}")

    def execute(self, case):
        m, x, f = self.machine, self.x, self.f
        m.context_restore(self.start_context)
        self.error, self.instructions = None, 0
        self.deadline = time.monotonic() + .25
        m.mem_write(SP - 0x1000, bytes(0x1100))
        m.mem_write(DATA, struct.pack("<4I", *case[:4]))
        m.mem_write(DATA + 0x100, bytes(16))
        m.reg_write(x.UC_X86_REG_ESP, SP)
        m.reg_write(x.UC_X86_REG_EFLAGS, 0x202)
        m.reg_write(x.UC_X86_REG_FPCW, 0x027F)
        m.reg_write(x.UC_X86_REG_MXCSR, 0x1F80)
        m.mem_write(SP, struct.pack("<I", RETURN))
        name = f["name"]
        if name == "DiffTrack":
            from .cases import validate_difftrack
            validate_difftrack(case)
            m.mem_write(SP + 4, struct.pack("<4I", *case[:4]))
        elif name == "BG_GetQuickScopeSpread":
            from .cases import validate_quickscope
            validate_quickscope(case)
            state = bytearray(0x26e8)
            struct.pack_into("<I", state, 0x560, case[1])
            struct.pack_into("<2I", state, 0x4ec, *case[2:4])
            m.mem_write(DATA + 0x1000, bytes(state))
            for i, rva in enumerate((0x84f874, 0x84f928, 0x851a8c, 0x8519e0)):
                settings = bytearray(0x100)
                struct.pack_into("<I", settings, 0x18, case[0] if i == 0 else case[i + 3])
                m.mem_write(DATA + 0x400 + i * 0x100, bytes(settings))
                m.mem_write(self.base + rva, struct.pack("<I", DATA + 0x400 + i * 0x100))
            m.reg_write(x.UC_X86_REG_EAX, DATA + 0x1000)
        elif name == "G_GetClientSize":
            pass
        elif name == "FloatPack9S":
            m.reg_write(x.UC_X86_REG_XMM1, case[0])
        elif name == "FloatPack10S":
            m.reg_write(x.UC_X86_REG_XMM2, case[0])
            m.reg_write(x.UC_X86_REG_XMM1, struct.unpack("<Q", self.pe.get_data(0x677150, 8))[0])
        elif name == "Vec2UnpackTexCoords":
            m.mem_write(SP + 4, struct.pack("<II", case[0], DATA + 0x100))
        elif name == "Byte4UnpackRgba":
            if case[1]:
                m.mem_write(DATA + 0x100, struct.pack("<I", case[0]))
            m.reg_write(x.UC_X86_REG_ECX, DATA + 0x100 if case[1] else DATA)
            m.reg_write(x.UC_X86_REG_EAX, DATA + 0x100)
        elif name == "FX_GetVelocityAtTime":
            life, elapsed = struct.unpack("<2f", struct.pack("<2I", *case[2:4]))
            if not (1 <= case[0] <= 255 and case[4] <= 255 and case[9] <= 1 and
                    not case[1] & ~0x3000000 and 0 < life <= 100000 and 0 <= elapsed <= life):
                raise Blocked("FX velocity input outside declared adapter domain")
            samples = [(((i * 13 + j * 7 + case[5]) & 0xffffffff) % 255 - 127) * .0625
                       for i in range(257) for j in range(24)]
            m.mem_write(DATA + 0x2000, struct.pack("<6168f", *samples))
            definition = bytearray(0x124)
            struct.pack_into("<I", definition, 0, case[1])
            definition[0xba] = case[0]
            struct.pack_into("<I", definition, 0xbc, DATA + 0x2000)
            m.mem_write(DATA + 0x1000, bytes(definition))
            axis = [.6, .8, 0., -.8, .6, 0., 0., 0., 1.] if case[9] else [1., 0., 0., 0., 1., 0., 0., 0., 1.]
            m.mem_write(DATA + 0x1200, struct.pack("<12f", 0., 0., 0., *axis))
            m.mem_write(DATA + 0x1300, struct.pack("<3I", *case[6:9]))
            m.mem_write(SP + 4, struct.pack("<7I", DATA + 0x1000, case[4], case[2], case[3],
                                         DATA + 0x1200, DATA + 0x1300, DATA + 0x100))
        elif name == "ExpandBoundsToWidth":
            m.mem_write(DATA + 0x100, struct.pack("<6I", *case[:6]))
            m.mem_write(SP + 4, struct.pack("<2I", DATA + 0x100, DATA + 0x10c))
        elif name == "FX_SetupVisualState":
            time_value = struct.unpack("<f", struct.pack("<I", case[1]))[0]
            if case[0] > 255 or not 0 <= time_value <= 1.125:
                raise Blocked("FX setup adapter requires byte interval count and finite time in [0, 1.125]")
            m.mem_write(DATA + 0x100, bytes(0x1c))
            m.mem_write(DATA + 0x1000, bytes(0x124))
            m.mem_write(DATA + 0x10bb, bytes([case[0]]))
            m.mem_write(DATA + 0x10c0, struct.pack("<I", DATA + 0x2000))
            m.mem_write(SP + 4, struct.pack("<5I", DATA + 0x1000, DATA + 0x1800,
                                         case[2], case[1], DATA + 0x100))
        elif name == "PM_Accelerate":
            m.mem_write(DATA + 0x1000, bytes(0x26E8))
            m.mem_write(DATA + 0x100C, struct.pack("<I", case[0]))
            m.mem_write(DATA + 0x1030, struct.pack("<3I", *case[1:4]))
            m.mem_write(DATA, struct.pack("<3I", *case[4:7]))
            m.mem_write(DATA + 0x200, bytes(0x8C))
            m.mem_write(DATA + 0x224, struct.pack("<I", case[9]))
            m.mem_write(DATA + 0x318, struct.pack("<I", case[10]))
            m.mem_write(self.base + f["globals"][0]["rva"], struct.pack("<I", DATA + 0x300))
            m.mem_write(SP + 4, struct.pack("<III", DATA + 0x1000, case[7], case[8]))
            m.reg_write(x.UC_X86_REG_ECX, DATA)
            m.reg_write(x.UC_X86_REG_EDX, DATA + 0x200)
        else:
            raise Blocked(f"No emulator adapter for {name}")
        stop = self.base + f["stop_rva"] if f.get("stop_rva") else RETURN
        steps = (case[11] or 1) if name == "PM_Accelerate" else 1
        if steps > 1024:
            raise Blocked("Too many stateful replay steps")
        step_context = m.context_save()
        try:
            for _ in range(steps):
                m.context_restore(step_context)
                # A host deadline in the instruction hook avoids creating a
                # timeout worker thread for every tiny exhaustive-test case.
                m.emu_start(self.base + f["entry_rva"], stop, count=10000)
                if self.error or m.reg_read(x.UC_X86_REG_EIP) != stop:
                    break
        except self.uc.UcError as error:
            raise Blocked(f"Emulator fault at {m.reg_read(x.UC_X86_REG_EIP):#x}: {error}") from error
        if self.error:
            raise Blocked(self.error)
        if m.reg_read(x.UC_X86_REG_EIP) != stop:
            raise Blocked("Instruction or time limit reached")
        if name == "DiffTrack":
            top = (m.reg_read(x.UC_X86_REG_FPSW) >> 11) & 7
            return x87_double_words(*m.reg_read(getattr(x, "UC_X86_REG_FP" + str(top))))
        if name == "BG_GetQuickScopeSpread":
            return [m.reg_read(x.UC_X86_REG_XMM0) & 0xffffffff, 0, 0, 0]
        if name == "G_GetClientSize":
            return [m.reg_read(x.UC_X86_REG_EAX), 0, 0, 0]
        if name.startswith("FloatPack"):
            reg = x.UC_X86_REG_XMM1 if name == "FloatPack9S" else x.UC_X86_REG_XMM4
            mask = 511 if name == "FloatPack9S" else 1023
            return [m.reg_read(reg) & mask, 0, 0, 0]
        if name == "PM_Accelerate":
            return list(struct.unpack("<3I", m.mem_read(DATA + 0x1030, 12))) + [case[0]]
        if name == "ExpandBoundsToWidth":
            words = struct.unpack("<6I", m.mem_read(DATA + 0x100, 24))
            if words[:2] != tuple(case[:2]) or words[3:5] != tuple(case[3:5]):
                raise Blocked("Bounds expansion modified XY")
            return [words[2], words[5], 0, 0]
        if name == "FX_SetupVisualState":
            lerp, inv, elem, effect, sample, seed, fade = struct.unpack("<7I", m.mem_read(DATA + 0x100, 0x1c))
            if (elem, effect, seed) != (DATA + 0x1000, DATA + 0x1800, case[2]):
                raise Blocked("FX setup pointer or seed output mismatch")
            offset = sample - (DATA + 0x2000)
            if offset < 0 or offset % 0x30 or offset >= 512 * 0x30:
                raise Blocked("FX setup sample pointer outside adapter allocation")
            return [lerp, inv, offset // 0x30, fade]
        return list(struct.unpack("<4I", m.mem_read(DATA + 0x100, 16)))
