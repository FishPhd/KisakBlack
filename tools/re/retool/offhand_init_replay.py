"""Complete original offhand initialization and predictable ADS-exit event replay."""
import struct

from .core import Blocked


def validate(case):
    if (len(case) != 16 or not 1 <= case[0] < 2048 or
            not 0 <= case[1] <= 65535 or not 0 <= case[8] <= 255 or
            case[15] not in (0, 1)):
        raise ValueError("Offhand init requires 16 words, nonzero offhand<2048, uint16 current weapon, sequence byte and bool output view")


def corpus(rng, random_count):
    values = []
    for weapon in (0, 1, 2047, 65535):
        for water in (0, 2, 3, 4, 0xffffffff, 0x80000000, 0x7fffffff):
            for flags in (0, 2, 0x20000, 0x20002, 0xffffffff):
                for mode in (0, 0x10, 0xffffffff):
                    for sequence in (0, 1, 2, 3, 127, 128, 254, 255):
                        case = (rng.choice((1, 1023, 2047)), weapon, water, flags, mode,
                                *(rng.getrandbits(32) for _ in range(3)), sequence,
                                *(rng.getrandbits(32) for _ in range(6)), 0)
                        values.extend((case, (*case[:15], 1)))
    for _ in range(random_count):
        case = (rng.randrange(1, 2048), rng.randrange(65536), rng.getrandbits(32),
                *(rng.getrandbits(32) for _ in range(5)), rng.randrange(256),
                *(rng.getrandbits(32) for _ in range(6)), 0)
        values.extend((case, (*case[:15], 1)))
    return values


def memory_ranges(write):
    from .emulator import DATA
    offsets = ([(0xc, 8), (0x40, 4), (0x54, 4), (0xe8, 2), (0xec, 32), (0x158, 4)] if write else
               [(0xc, 8), (0xe8, 2), (0x144, 2), (0x590, 4)])
    return [(DATA + 0x1000 + offset, DATA + 0x1000 + offset + size) for offset, size in offsets]


def setup(emulator, case):
    from .emulator import DATA
    validate(case)
    m, x = emulator.machine, emulator.x
    state = bytearray(0x26e8)
    struct.pack_into("<H", state, 0x144, case[1])
    struct.pack_into("<H", state, 0xe8, case[8])
    for offset, value in ((0x134, case[0]), (0x590, case[2]), (0x10, case[3]),
                          (0xc, case[4]), (0x158, case[5]), (0x40, case[6]),
                          (0x54, case[7]), (0x3c, case[11]), (0x50, case[12]),
                          (0x58, case[13]), (0, case[14])):
        struct.pack_into("<I", state, offset, value)
    for i in range(4):
        struct.pack_into("<I", state, 0xec + i * 4, case[9] ^ (0x1020304 * (i + 1)))
        struct.pack_into("<I", state, 0xfc + i * 4, case[10] ^ (0x4030201 * (i + 1)))
    emulator.offhand_init_before = bytes(state)
    m.mem_write(DATA + 0x1000, bytes(state))
    m.reg_write(x.UC_X86_REG_ESI, DATA + 0x1000)


def outputs(emulator, case):
    from .emulator import DATA
    state = emulator.machine.mem_read(DATA + 0x1000, 0x26e8)
    def word(offset): return struct.unpack_from("<I", state, offset)[0]
    before = bytearray(emulator.offhand_init_before)
    # Only the signed waterlevel<3 path may modify state. The selected slot uses
    # the incoming sequence, including the wrap from 255 to zero.
    slot = case[8] & 3
    if struct.unpack("<i", struct.pack("<I", case[2]))[0] < 3:
        for offset, size in ((0xc, 4), (0x10, 4), (0x40, 4), (0x54, 4),
                             (0x158, 4), (0xe8, 2), (0xec + slot * 4, 4),
                             (0xfc + slot * 4, 4)):
            before[offset:offset + size] = state[offset:offset + size]
    if before != state:
        raise Blocked("Offhand init modified unobserved player-state bytes or its waterlevel guard path")
    if case[15]:
        return [word(0x54), struct.unpack_from("<H", state, 0xe8)[0],
                word(0xec + slot * 4), word(0xfc + slot * 4)]
    return [word(offset) for offset in (0x158, 0x10, 0x40, 0xc)]
