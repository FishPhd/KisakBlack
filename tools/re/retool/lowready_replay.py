"""Complete original low-ready/DTP interpolation with read-only private structures."""
import struct

from .core import Blocked


def validate(case):
    if len(case) < 4 or any(not isinstance(word, int) or not 0 <= word <= 0xffffffff
                            for word in case[:4]):
        raise ValueError("Low-ready interpolation requires four raw uint32 input words")
    if any(case[4:]):
        raise ValueError("Low-ready interpolation has no additional fixture words")


def corpus(rng, random_count, dtp=False):
    # Inputs are raw signed state, weapon time, in duration, out duration. Both
    # negative durations and all int32 conversion boundaries are valid originals.
    times = (0, 1, 0xffffffff, 0x80000000, 0x7fffffff,
             0x00ffffff, 0x01000000, 0x01000001)
    durations = ((0, 0), (1, 0), (0, 1), (1, 1),
                 (0xffffffff, 0x80000000), (0x7fffffff, 0x01000001))
    values = [(state, time, incoming, outgoing)
              for state in (*range(64), 0xffffffff, 0x80000000, 0x7fffffff)
              for time in times for incoming, outgoing in durations]
    edges = (0, 1, 2, 3, 0xffffffff, 0xfffffffe, 0x80000000, 0x7fffffff,
             0x00ffffff, 0x01000000, 0x01000001, 0xfeffffff, 0xff000001)
    entering, exiting = (41, 43) if dtp else (48, 50)
    values += [(state, time, duration, duration)
               for state in (entering - 1, entering, exiting - 1, exiting)
               for time in edges for duration in edges]
    values += [(rng.choice((entering, exiting, rng.getrandbits(32))), rng.getrandbits(32),
                rng.getrandbits(32), rng.getrandbits(32)) for _ in range(random_count)]
    return values


def memory_ranges(write, dtp=False):
    from .emulator import DATA
    if write:
        return []
    incoming, outgoing = (0x43c, 0x444) if dtp else (0x424, 0x42c)
    return [(DATA + 0x4000, DATA + 0x4004),  # weaponState_t.ps
            (DATA + 0x103c, DATA + 0x1040),  # playerState_s.weaponTime
            (DATA + 0x1158, DATA + 0x115c),  # playerState_s.weaponstate
            (DATA + 0x5000 + incoming, DATA + 0x5004 + incoming),
            (DATA + 0x5000 + outgoing, DATA + 0x5004 + outgoing)]


def setup(emulator, case):
    from .emulator import DATA, SP
    validate(case)
    m, x = emulator.machine, emulator.x
    state = bytearray(0x26e8)
    struct.pack_into("<I", state, 0x158, case[0])
    struct.pack_into("<I", state, 0x3c, case[1])
    definition = bytearray(0x808)
    dtp = emulator.f["name"] == "BG_CalculateWeaponMovement_GetDTPLerp"
    incoming, outgoing = (0x43c, 0x444) if dtp else (0x424, 0x42c)
    struct.pack_into("<I", definition, incoming, case[2])
    struct.pack_into("<I", definition, outgoing, case[3])
    movement = bytearray(0x90)
    struct.pack_into("<I", movement, 0, DATA + 0x1000)
    emulator.lowready_before = (bytes(state), bytes(movement), bytes(definition))
    for offset, content in zip((0x1000, 0x4000, 0x5000), emulator.lowready_before):
        m.mem_write(DATA + offset, content)
    # Custom original ABI: definition in EAX; ws is the first stack argument.
    m.reg_write(x.UC_X86_REG_EAX, DATA + 0x5000)
    m.mem_write(SP + 4, struct.pack("<I", DATA + 0x4000))


def outputs(emulator, case):
    from .emulator import DATA
    m = emulator.machine
    for offset, original in zip((0x1000, 0x4000, 0x5000), emulator.lowready_before):
        if m.mem_read(DATA + offset, len(original)) != original:
            raise Blocked("Low-ready interpolation modified read-only fixture structures")
    return [m.reg_read(emulator.x.UC_X86_REG_XMM0) & 0xffffffff, 0, 0, 0]
