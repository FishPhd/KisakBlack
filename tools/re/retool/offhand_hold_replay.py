"""Complete original offhand-hold replay with a private valid offhand asset."""
import struct

from .core import Blocked


def validate(case):
    if len(case) < 8 or not 1 <= case[0] < 2048:
        raise ValueError("Offhand hold requires eight words and valid nonzero offhand index<2048")


def corpus(rng, random_count):
    values = []
    for weapon in (1, 1023, 2047):
        for flags in (0, 2, 0x20000, 0x20002, 0xffffffff):
            for owner in (0, 1022, 1023, 1024, 0xffffffff):
                for timer in (0, 1, 0xffffffff, 0x80000000, 0x7fffffff):
                    for fuse in (0, 1, 3000, 0xffffffff):
                        values.append((weapon, flags, owner, timer, fuse,
                                       rng.getrandbits(32), rng.getrandbits(32), rng.getrandbits(32)))
    values += [(rng.randrange(1, 2048), rng.getrandbits(32),
                rng.choice((1023, rng.getrandbits(32))), rng.getrandbits(32),
                rng.getrandbits(32), rng.getrandbits(32), rng.getrandbits(32), rng.getrandbits(32))
               for _ in range(random_count)]
    return values


def memory_ranges(write):
    from .emulator import DATA
    if write:
        offsets = ((0x10, 4), (0x3c, 8), (0x50, 4), (0x158, 4))
    else:
        offsets = ((0x10, 4), (0x54, 4), (0x134, 4))
    ranges = [(DATA + 0x1000 + offset, DATA + 0x1000 + offset + size) for offset, size in offsets]
    if not write:
        ranges += [(DATA + 0x4008, DATA + 0x400c), (DATA + 0x546c, DATA + 0x5470)]
    return ranges


def setup(emulator, case):
    from .emulator import DATA
    validate(case)
    m, x = emulator.machine, emulator.x
    state = bytearray(0x26e8)
    for offset, value in ((0x134, case[0]), (0x10, case[1]), (0x54, case[2]),
                          (0x50, case[3]), (0x158, case[5]), (0x3c, case[6]), (0x40, case[7])):
        struct.pack_into("<I", state, offset, value)
    emulator.offhand_hold_before = bytes(state)
    m.mem_write(DATA + 0x1000, bytes(state))
    definition = bytearray(0x808)
    struct.pack_into("<I", definition, 0x46c, case[4])
    m.mem_write(DATA + 0x5000, bytes(definition))
    variant = bytearray(0xe4)
    struct.pack_into("<I", variant, 8, DATA + 0x5000)
    m.mem_write(DATA + 0x4000, bytes(variant))
    table = bytearray(8192)
    struct.pack_into("<I", table, case[0] * 4, DATA + 0x4000)
    m.mem_write(emulator.base + 0x880258, bytes(table))
    m.reg_write(x.UC_X86_REG_ESI, DATA + 0x1000)


def outputs(emulator, case):
    from .emulator import DATA
    state = emulator.machine.mem_read(DATA + 0x1000, 0x26e8)
    def word(offset):
        return struct.unpack_from("<I", state, offset)[0]
    if word(0x40) != 0:
        raise Blocked("Offhand hold did not satisfy original zero-delay postcondition")
    before = bytearray(emulator.offhand_hold_before)
    for offset in (0x158, 0x10, 0x50, 0x3c, 0x40):
        before[offset:offset + 4] = state[offset:offset + 4]
    if before != state:
        raise Blocked("Offhand hold modified undeclared player-state bytes")
    return [word(offset) for offset in (0x158, 0x10, 0x50, 0x3c)]
