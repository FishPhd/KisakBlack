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
            if self.f["name"] == "PM_Accelerate":
                allowed.append((DATA + 0x1030, DATA + 0x103C))
        else:
            allowed += self.constants + [(DATA, DATA + 16), (DATA + 0x100, DATA + 0x110)]
            if self.f["name"] == "PM_Accelerate":
                allowed += [(DATA + 0x1000, DATA + 0x1000 + 0x26A4),
                            (DATA + 0x200, DATA + 0x200 + 0x8C), (DATA + 0x300, DATA + 0x320)]
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
        if name == "FloatPack9S":
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
        elif name == "PM_Accelerate":
            m.mem_write(DATA + 0x1000, bytes(0x26A4))
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
        if name.startswith("FloatPack"):
            reg = x.UC_X86_REG_XMM1 if name == "FloatPack9S" else x.UC_X86_REG_XMM4
            mask = 511 if name == "FloatPack9S" else 1023
            return [m.reg_read(reg) & mask, 0, 0, 0]
        if name == "PM_Accelerate":
            return list(struct.unpack("<3I", m.mem_read(DATA + 0x1030, 12))) + [case[0]]
        return list(struct.unpack("<4I", m.mem_read(DATA + 0x100, 16)))
