"""Bounded complete original offhand-end replay with real weapon/clip fixtures."""
import struct

from .core import Blocked


def validate(case):
    if (len(case) < 16 or not 1 <= case[0] < 2048 or
            not 0 <= case[4] <= 65535 or not 0 <= case[5] <= 65535 or
            any(case[i] not in (0, 1) for i in (6, 7, 8)) or
            not 0 <= case[12] <= 15 or not 0 <= case[13] <= 15 or
            case[12] and case[12] == case[13]):
        raise ValueError("Offhand end requires 16 words, nonzero weapon<2048, uint16 animations, bool flags and disjoint clip slots 0..15")


def corpus(rng, random_count):
    values = []
    for flags in (0, 2, 0x20000, 0x20002, 0xffffffff):
        for mode in (0, 8, 9, 0xffffffff):
            for left in (0, 1):
                for dual in (0, 1):
                    for unlimited in (0, 1):
                        for right_count, left_count in ((0, 1), (1, 0), (0xffffffff, 0), (0, 0xffffffff)):
                            for right_slot, left_slot in ((1, 15), (8, 0), (0, 8), (15, 1)):
                                values.append((rng.choice((1, 1023, 2047)), flags, rng.getrandbits(32), mode,
                                               rng.choice((0, 0x400, 0xffff)), rng.choice((0, 0x400, 0xffff)),
                                               left, dual, unlimited, rng.getrandbits(32), right_count, left_count,
                                               right_slot, left_slot, rng.getrandbits(32), rng.getrandbits(32)))
    for _ in range(random_count):
        right_slot, left_slot = rng.sample(range(16), 2)
        values.append((rng.randrange(1, 2048), rng.getrandbits(32), rng.getrandbits(32), rng.getrandbits(32),
                       rng.randrange(65536), rng.randrange(65536), rng.randrange(2), rng.randrange(2), rng.randrange(2),
                       rng.getrandbits(32), rng.getrandbits(32), rng.getrandbits(32), right_slot, left_slot,
                       rng.getrandbits(32), rng.getrandbits(32)))
    return values


def memory_ranges(write):
    from .emulator import DATA
    if write:
        offsets = [(0xc, 4), (0x10, 4), (0x3c, 4), (0x40, 4), (0x54, 4),
                   (0x58, 4), (0x158, 4), (0x558, 4), (0x55c, 4)]
        return [(DATA + 0x1000 + offset, DATA + 0x1000 + offset + size) for offset, size in offsets]
    offsets = [(4, 4), (0xc, 4), (0x10, 4), (0x61, 1), (0x144, 2), (0x558, 4), (0x55c, 4)]
    offsets += [(0x3f4 + i * 8, 8) for i in range(15)]
    ranges = [(DATA + 0x1000 + offset, DATA + 0x1000 + offset + size) for offset, size in offsets]
    ranges += [(DATA + offset, DATA + offset + size) for offset, size in
               ((0x4008, 4), (0x4108, 4), (0x404c, 4), (0x414c, 4),
                (0x5358, 1), (0x5408, 4), (0x556a, 1), (0x5594, 4))]
    return ranges


def setup(emulator, case):
    from .emulator import DATA
    validate(case)
    m, x = emulator.machine, emulator.x
    state = bytearray(0x26e8)
    struct.pack_into("<H", state, 0x144, case[0])
    for offset, value in ((0x10, case[1]), (0xc, case[2]), (4, case[3]),
                          (0x558, case[4]), (0x55c, case[5]), (0x54, case[14]),
                          (0x58, case[15]), (0x40, 0x12345678), (0x3c, (-0x1234567) & 0xffffffff),
                          (0x158, 0x12345678)):
        struct.pack_into("<I", state, offset, value)
    state[0x61] = case[6]
    for i in range(15):
        struct.pack_into("<2I", state, 0x3f4 + i * 8, 0x10000 + i, 0x3456789)
    for slot, clip, count in ((case[12], 123, case[10]), (case[13], 456, case[11])):
        if slot:
            struct.pack_into("<2I", state, 0x3f4 + (slot - 1) * 8, clip, count)
    emulator.offhand_before = bytes(state)
    m.mem_write(DATA + 0x1000, bytes(state))
    definition = bytearray(0x800)
    definition[0x358], definition[0x56a] = case[8], case[7]
    struct.pack_into("<I", definition, 0x408, case[9])
    left_weapon = 1 if case[0] == 2047 else 2047
    struct.pack_into("<I", definition, 0x594, left_weapon)
    m.mem_write(DATA + 0x5000, bytes(definition))
    for offset, clip in ((0x4000, 123), (0x4100, 456)):
        variant = bytearray(0xe4)
        struct.pack_into("<I", variant, 8, DATA + 0x5000)
        struct.pack_into("<I", variant, 0x4c, clip)
        m.mem_write(DATA + offset, bytes(variant))
    table = bytearray(8192)
    struct.pack_into("<I", table, case[0] * 4, DATA + 0x4000)
    struct.pack_into("<I", table, left_weapon * 4, DATA + 0x4100)
    m.mem_write(emulator.base + 0x880258, bytes(table))
    m.reg_write(x.UC_X86_REG_ESI, DATA + 0x1000)


def outputs(emulator, case):
    from .emulator import DATA
    state = emulator.machine.mem_read(DATA + 0x1000, 0x26e8)
    def word(offset):
        return struct.unpack_from("<I", state, offset)[0]
    if (word(0x40) != 0 or word(0x58) != 0 or word(0x54) != 1023 or
            word(0xc) != case[2] & ~0x200 or word(0x558) > 65535 or word(0x55c) > 65535):
        raise Blocked("Offhand end violated original fixed postconditions or animation packing domain")
    before = bytearray(emulator.offhand_before)
    for offset in (0x158, 0x10, 0x3c, 0x40, 0xc, 0x54, 0x58, 0x558, 0x55c):
        before[offset:offset + 4] = state[offset:offset + 4]
    if before != state:
        raise Blocked("Offhand end modified undeclared player-state bytes")
    return [word(0x158), word(0x10), word(0x3c), word(0x558) | (word(0x55c) << 16)]
