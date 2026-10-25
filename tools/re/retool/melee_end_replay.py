"""Complete melee-end/idle/prone replay with private real weapon definitions."""
import struct

from .core import Blocked


def validate(case):
    if (len(case) != 16 or not 0 <= case[0] < 2048 or not 0 <= case[1] < 2048 or
            any(case[i] not in (0, 1) for i in (2, 3, 4, 15)) or
            not 0 <= case[10] <= 65535 or not 0 <= case[11] <= 65535):
        raise ValueError("Melee end requires 16 words, valid weapon indices, bool asset/alias/view flags and uint16 animations")


def corpus(rng, random_count):
    values = []
    for weapon, melee in ((0, 0), (1, 1), (1, 2), (2047, 0)):
        for alias in (0, 1):
            for bayonet, use_melee in ((0, 0), (0, 1), (1, 0), (1, 1)):
                for mode in (0, 8, 9, 0xffffffff):
                    for water in (0, 2, 3, 0xffffffff):
                        for flags in (0, 1, 0x200, 0x201):
                            case = (weapon, melee, alias, bayonet, use_melee, mode, water,
                                    flags, rng.getrandbits(32), rng.getrandbits(32),
                                    rng.choice((0, 0x400, 0xffff)), rng.choice((0, 0x400, 0xffff)),
                                    rng.getrandbits(32), rng.getrandbits(32), rng.getrandbits(32), 0)
                            values.extend((case, (*case[:15], 1)))
    for _ in range(random_count):
        case = (rng.randrange(2048), rng.randrange(2048), rng.randrange(2), rng.randrange(2), rng.randrange(2),
                rng.getrandbits(32), rng.getrandbits(32), rng.getrandbits(32), rng.getrandbits(32),
                rng.getrandbits(32), rng.randrange(65536), rng.randrange(65536), rng.getrandbits(32),
                rng.getrandbits(32), rng.getrandbits(32), 0)
        values.extend((case, (*case[:15], 1)))
    return values


def memory_ranges(write):
    from .emulator import DATA
    offsets = ([(0xc, 8), (0x3c, 16), (0x158, 8), (0x558, 8)] if write else
               [(4, 4), (0xc, 8), (0x144, 2), (0x154, 4), (0x558, 8), (0x590, 4)])
    ranges = [(DATA + 0x1000 + offset, DATA + 0x1000 + offset + size) for offset, size in offsets]
    if not write:
        ranges += [(DATA + offset, DATA + offset + size) for offset, size in
                   ((0x4008, 4), (0x4108, 4), (0x5408, 4), (0x5569, 1), (0x564c, 1))]
    return ranges


def setup(emulator, case):
    from .emulator import DATA
    validate(case)
    m, x = emulator.machine, emulator.x
    state = bytearray(0x26e8)
    struct.pack_into("<H", state, 0x144, case[0])
    for offset, value in ((0x154, case[1]), (4, case[5]), (0x590, case[6]), (0xc, case[7]),
                          (0x10, case[8]), (0x558, case[10]), (0x55c, case[11]),
                          (0x44, case[12]), (0x48, case[13]), (0x15c, case[14]),
                          (0x158, 0x12345678), (0x3c, (-0x1234567) & 0xffffffff), (0x40, 0x12345678)):
        struct.pack_into("<I", state, offset, value)
    emulator.melee_end_before = bytes(state)
    m.mem_write(DATA + 0x1000, bytes(state))
    definition = bytearray(0x800)
    definition[0x569], definition[0x64c] = case[3], case[4]
    struct.pack_into("<I", definition, 0x408, case[9])
    m.mem_write(DATA + 0x5000, bytes(definition))
    m.mem_write(DATA + 0x6000, bytes(0x800))
    for offset, target in ((0x4000, 0x5000), (0x4100, 0x5000 if case[2] else 0x6000)):
        variant = bytearray(0xe4)
        struct.pack_into("<I", variant, 8, DATA + target)
        m.mem_write(DATA + offset, bytes(variant))
    table = bytearray(8192)
    struct.pack_into("<I", table, case[0] * 4, DATA + 0x4000)
    if case[1] != case[0]: struct.pack_into("<I", table, case[1] * 4, DATA + 0x4100)
    m.mem_write(emulator.base + 0x880258, bytes(table))
    m.reg_write(x.UC_X86_REG_ESI, DATA + 0x1000)


def outputs(emulator, case):
    from .emulator import DATA
    state = emulator.machine.mem_read(DATA + 0x1000, 0x26e8)
    def word(offset): return struct.unpack_from("<I", state, offset)[0]
    if word(0x40) or word(0x558) > 65535 or word(0x55c) > 65535:
        raise Blocked("Melee end violated original zero-delay postcondition or uint16 animation domain")
    before = bytearray(emulator.melee_end_before)
    for offset in (0xc, 0x10, 0x3c, 0x40, 0x44, 0x48, 0x158, 0x15c, 0x558, 0x55c):
        before[offset:offset + 4] = state[offset:offset + 4]
    if before != state:
        raise Blocked("Melee end modified unobserved player-state bytes")
    if case[15]: return [word(offset) for offset in (0x10, 0x44, 0x48, 0x15c)]
    return [word(0x158), word(0x3c), word(0xc), word(0x558) | (word(0x55c) << 16)]
