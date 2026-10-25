"""Complete original slide-entry checker, transition, and getter replay."""
import struct

from .core import Blocked

# Private allocation extent. Original execution may read only the three mapped
# pmove fields below; the rest is an immutable fixture, not a type-layout claim.
PM_SIZE = 0x910


def validate(case):
    if (len(case) != 16 or any(not isinstance(word, int) or not 0 <= word <= 0xffffffff for word in case)
            or not 1 <= case[0] < 2048 or case[14] > 0x1ffff):
        raise ValueError("Slide checker requires 16 words, weapon1..2047 and packed uint16 command weapon/bool hand")


def corpus(rng, random_count):
    values = []
    def add(state=0, water=2, flags=0x800000, slide=123, server=123,
            mode=0, right=0, left=0, hand=0, command=1, duration=100):
        # current weapon, raw state/water/pm_flags/slideTime/serverTime/pm_type,
        # right/left animations, duration, weaponTime/delay, weapFlags, leftstate,
        # uint16 command weapon | run-left<<16, left weaponTime.
        values.append((rng.choice((1, 1023, 2047)), state, water, flags, slide, server,
                       mode, right, left, duration, rng.getrandbits(32), rng.getrandbits(32),
                       rng.getrandbits(32), rng.getrandbits(32), command | (hand << 16), rng.getrandbits(32)))
    for state in (*range(64), 0xffffffff, 0x80000000, 0x7fffffff):
        for mode in (0, 8, 9, 0xffffffff):
            for hand in (0, 1):
                for right, left in ((0, 0), (51, 0), (0, 51), (0x80000033, 0)):
                    add(state=state, mode=mode, hand=hand, right=right, left=left)
    for water in (0, 2, 3, 4, 0xffffffff, 0x80000000, 0x7fffffff):
        for command in (0, 1, 65535):
            for flags in (0, 0x800000, 0xffffffff):
                for slide, server in ((0, 0), (1, 1), (0xffffffff, 0xffffffff),
                                      (0x80000000, 0x7fffffff), (123, 124)):
                    add(water=water, command=command, flags=flags, slide=slide, server=server)
    for duration in (0, 1, 1000, 0xffffffff, 0x80000000, 0x7fffffff):
        for right, left, hand in ((0x433, 0, 0), (0, 0x433, 1), (0xffffffff, 0x80000000, 0),
                                  (0x80000000, 0xffffffff, 1), (51, 51, 0)):
            add(duration=duration, right=right, left=left, hand=hand)
    for _ in range(random_count):
        server = rng.getrandbits(32)
        add(state=rng.choice((0, 8, 17, 26, 31, 32, 40, 44, rng.getrandbits(32))),
            water=rng.choice((2, 3, rng.getrandbits(32))), flags=rng.getrandbits(32),
            slide=rng.choice((server, rng.getrandbits(32))), server=server,
            mode=rng.getrandbits(32), right=rng.getrandbits(32), left=rng.getrandbits(32),
            hand=rng.randrange(2), command=rng.choice((0, rng.randrange(65536))), duration=rng.getrandbits(32))
    return values


def memory_ranges(write):
    from .emulator import DATA
    offsets = ([(0x3c, 8), (0x158, 4), (0x558, 4)] if write else
               [(4, 4), (0xc, 4), (0x61, 1), (0xac, 4), (0x144, 2), (0x158, 4),
                (0x558, 8), (0x590, 4)])
    ranges = [(DATA + 0x1000 + offset, DATA + 0x1000 + offset + size) for offset, size in offsets]
    if not write:
        ranges += [(DATA + 0xa000, DATA + 0xa008), (DATA + 0xa01c, DATA + 0xa01e),
                   (DATA + 0x4008, DATA + 0x400c), (DATA + 0x5448, DATA + 0x544c)]
    return ranges


def setup(emulator, case):
    from .emulator import DATA
    validate(case)
    m, x = emulator.machine, emulator.x
    state = bytearray(0x26e8)
    struct.pack_into("<H", state, 0x144, case[0])
    state[0x61] = case[14] >> 16
    for offset, value in ((0x158, case[1]), (0x590, case[2]), (0xc, case[3]),
                          (0xac, case[4]), (4, case[6]), (0x558, case[7]), (0x55c, case[8]),
                          (0x3c, case[10]), (0x40, case[11]), (0x10, case[12]),
                          (0x15c, case[13]), (0x44, case[15])):
        struct.pack_into("<I", state, offset, value)
    movement = bytearray(PM_SIZE)
    struct.pack_into("<2I", movement, 0, DATA + 0x1000, case[5])
    struct.pack_into("<H", movement, 0x1c, case[14] & 0xffff)
    definition = bytearray(0x808)
    struct.pack_into("<I", definition, 0x448, case[9])
    variant = bytearray(0xe4)
    struct.pack_into("<I", variant, 8, DATA + 0x5000)
    emulator.slide_check_before = [(0x1000, bytes(state)), (0xa000, bytes(movement)),
                                  (0x4000, bytes(variant)), (0x5000, bytes(definition))]
    for offset, content in emulator.slide_check_before:
        m.mem_write(DATA + offset, content)
    table = bytearray(8192)
    struct.pack_into("<I", table, case[0] * 4, DATA + 0x4000)
    emulator.slide_check_table = bytes(table)
    m.mem_write(emulator.base + 0x880258, bytes(table))
    m.reg_write(x.UC_X86_REG_ECX, DATA + 0xa000)


def outputs(emulator, case):
    from .emulator import DATA
    m = emulator.machine
    state = m.mem_read(DATA + 0x1000, 0x26e8)
    before = bytearray(emulator.slide_check_before[0][1])
    for offset in (0x158, 0x3c, 0x40, 0x558):
        before[offset:offset + 4] = state[offset:offset + 4]
    if before != state:
        raise Blocked("Slide checker modified unobserved state or left animation")
    for offset, original in emulator.slide_check_before[1:]:
        if m.mem_read(DATA + offset, len(original)) != original:
            raise Blocked("Slide checker modified read-only pmove or asset fixtures")
    if m.mem_read(emulator.base + 0x880258, 8192) != emulator.slide_check_table:
        raise Blocked("Slide checker modified the private weapon table")
    return [struct.unpack_from("<I", state, offset)[0] for offset in (0x158, 0x3c, 0x40, 0x558)]
