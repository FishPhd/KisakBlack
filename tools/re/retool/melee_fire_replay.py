"""Complete original melee-fire transition and event/prone dependencies."""
import struct

from .core import Blocked


def validate(case):
    if (len(case) != 16 or any(not isinstance(word, int) or not 0 <= word <= 0xffffffff for word in case)
            or case[0] >= 2048 or case[1] >= 2048
            or any(case[i] not in (0, 1) for i in (2, 3, 15)) or case[6] > 255):
        raise ValueError("Melee fire requires 16 words, valid weapon indices, bool asset/view bits and event sequence byte")


def corpus(rng, random_count):
    values = []
    def add(weapon, melee, bayonet, use_melee, flags, sequence):
        case = (weapon, melee, bayonet, use_melee, flags, rng.getrandbits(32), sequence,
                *(rng.getrandbits(32) for _ in range(8)), 0)
        values.extend((case, (*case[:15], 1)))
    for weapon, melee in ((0, 0), (0, 2047), (1, 2), (2047, 1)):
        for bayonet, use_melee in ((0, 0), (1, 0), (0, 1), (1, 1)):
            for flags in (0, 1, 0x200, 0x201, 0x80000000, 0xffffffff):
                for sequence in (0, 1, 2, 3, 127, 128, 254, 255):
                    add(weapon, melee, bayonet, use_melee, flags, sequence)
    for _ in range(random_count):
        add(rng.randrange(2048), rng.randrange(2048), rng.randrange(2), rng.randrange(2),
            rng.getrandbits(32), rng.randrange(256))
    return values


def memory_ranges(write):
    from .emulator import DATA
    offsets = ([(0xc, 4), (0x158, 4), (0xe8, 2), (0xec, 32)] if write else
               [(0xc, 4), (0x144, 2), (0x154, 4), (0xe8, 2)])
    ranges = [(DATA + 0x1000 + offset, DATA + 0x1000 + offset + size) for offset, size in offsets]
    if not write:
        ranges += [(DATA + offset, DATA + offset + size) for offset, size in
                   ((0x4008, 4), (0x4108, 4), (0x5569, 1), (0x564c, 1))]
    return ranges


def setup(emulator, case):
    from .emulator import DATA
    validate(case)
    m, x = emulator.machine, emulator.x
    state = bytearray(0x26e8)
    struct.pack_into("<H", state, 0x144, case[0])
    struct.pack_into("<H", state, 0xe8, case[6])
    for offset, value in ((0x154, case[1]), (0xc, case[4]), (0x158, case[5]),
                          (0x3c, case[9]), (0x40, case[10]), (0x10, case[11]),
                          (0, case[12]), (4, case[13]), (0x590, case[14])):
        struct.pack_into("<I", state, offset, value)
    for i in range(4):
        struct.pack_into("<I", state, 0xec + i * 4, case[7] ^ (0x1020304 * (i + 1)))
        struct.pack_into("<I", state, 0xfc + i * 4, case[8] ^ (0x4030201 * (i + 1)))
    definition = bytearray(0x808)
    definition[0x569], definition[0x64c] = case[2], case[3]
    fixtures = [(0x1000, bytes(state)), (0x5000, bytes(definition)), (0x6000, bytes(0x808))]
    for offset, target in ((0x4000, 0x5000), (0x4100, 0x6000)):
        variant = bytearray(0xe4)
        struct.pack_into("<I", variant, 8, DATA + target)
        fixtures.append((offset, bytes(variant)))
    emulator.melee_fire_before = fixtures
    for offset, content in fixtures:
        m.mem_write(DATA + offset, content)
    table = bytearray(8192)
    struct.pack_into("<I", table, case[0] * 4, DATA + 0x4000)
    if case[1] != case[0]:
        struct.pack_into("<I", table, case[1] * 4, DATA + 0x4100)
    emulator.melee_fire_table = bytes(table)
    m.mem_write(emulator.base + 0x880258, bytes(table))
    m.reg_write(x.UC_X86_REG_ESI, DATA + 0x1000)


def outputs(emulator, case):
    from .emulator import DATA
    m = emulator.machine
    state = m.mem_read(DATA + 0x1000, 0x26e8)
    before = bytearray(emulator.melee_fire_before[0][1])
    slot = case[6] & 3
    for offset, size in ((0xc, 4), (0x158, 4), (0xe8, 2),
                         (0xec + slot * 4, 4), (0xfc + slot * 4, 4)):
        before[offset:offset + size] = state[offset:offset + size]
    if before != state:
        raise Blocked("Melee fire modified unobserved player-state bytes")
    for offset, original in emulator.melee_fire_before[1:]:
        if m.mem_read(DATA + offset, len(original)) != original:
            raise Blocked("Melee fire modified read-only asset fixtures")
    if m.mem_read(emulator.base + 0x880258, 8192) != emulator.melee_fire_table:
        raise Blocked("Melee fire modified the original weapon table")
    def word(offset): return struct.unpack_from("<I", state, offset)[0]
    return [word(0x158), word(0xc), struct.unpack_from("<H", state, 0xe8)[0],
            word((0xfc if case[15] else 0xec) + slot * 4)]
