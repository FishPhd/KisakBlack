"""Complete original slide-entry transition and continue-animation guards."""
import struct

from .core import Blocked


def validate(case):
    if (len(case) != 16 or any(not isinstance(word, int) or not 0 <= word <= 0xffffffff for word in case)
            or not 1 <= case[0] < 2048 or case[4] not in (0, 1) or case[15]):
        raise ValueError("Slide transition requires 16 words, nonzero weapon<2048, bool run-left and reserved0")


def corpus(rng, random_count):
    values = []
    def add(weapon, mode, right, left, hand, duration):
        # weapon, pm_type, right/left animation, run-left, asset duration,
        # state/time/delay, weapon/movement flags, left state/time/delay, water,
        # reserved0. Only right state/time/delay/animation may change.
        values.append((weapon, mode, right, left, hand, duration,
                       *(rng.getrandbits(32) for _ in range(9)), 0))
    animations = ((0, 0), (51, 0), (0, 51), (51, 51), (0x433, 0), (0, 0x433),
                  (0x80000033, 0), (0, 0x80000033),
                  (0xffffffff, 0x80000000), (0x80000000, 0xffffffff))
    for weapon in (1, 1023, 2047):
        for mode in (0, 8, 9, 10, 0xffffffff, 0x80000000, 0x7fffffff):
            for hand in (0, 1):
                for right, left in animations:
                    for duration in (0, 1, 1000, 0xffffffff, 0x80000000, 0x7fffffff):
                        add(weapon, mode, right, left, hand, duration)
    for _ in range(random_count):
        add(rng.randrange(1, 2048), rng.getrandbits(32), rng.getrandbits(32),
            rng.getrandbits(32), rng.randrange(2), rng.getrandbits(32))
    return values


def memory_ranges(write):
    from .emulator import DATA
    offsets = ([(0x3c, 8), (0x158, 4), (0x558, 4)] if write else
               [(4, 4), (0x61, 1), (0x144, 2), (0x558, 8)])
    ranges = [(DATA + 0x1000 + offset, DATA + 0x1000 + offset + size) for offset, size in offsets]
    if not write:
        ranges += [(DATA + 0x4008, DATA + 0x400c), (DATA + 0x5448, DATA + 0x544c)]
    return ranges


def setup(emulator, case):
    from .emulator import DATA
    validate(case)
    m, x = emulator.machine, emulator.x
    state = bytearray(0x26e8)
    struct.pack_into("<H", state, 0x144, case[0])
    state[0x61] = case[4]
    for offset, value in ((4, case[1]), (0x558, case[2]), (0x55c, case[3]),
                          (0x158, case[6]), (0x3c, case[7]), (0x40, case[8]),
                          (0x10, case[9]), (0xc, case[10]), (0x15c, case[11]),
                          (0x44, case[12]), (0x48, case[13]), (0x590, case[14])):
        struct.pack_into("<I", state, offset, value)
    definition = bytearray(0x808)
    struct.pack_into("<I", definition, 0x448, case[5])
    variant = bytearray(0xe4)
    struct.pack_into("<I", variant, 8, DATA + 0x5000)
    emulator.slide_transition_before = [(0x1000, bytes(state)), (0x4000, bytes(variant)),
                                        (0x5000, bytes(definition))]
    for offset, content in emulator.slide_transition_before:
        m.mem_write(DATA + offset, content)
    table = bytearray(8192)
    struct.pack_into("<I", table, case[0] * 4, DATA + 0x4000)
    emulator.slide_transition_table = bytes(table)
    m.mem_write(emulator.base + 0x880258, bytes(table))
    m.reg_write(x.UC_X86_REG_ESI, DATA + 0x1000)


def outputs(emulator, case):
    from .emulator import DATA
    m = emulator.machine
    state = m.mem_read(DATA + 0x1000, 0x26e8)
    before = bytearray(emulator.slide_transition_before[0][1])
    for offset in (0x158, 0x3c, 0x40, 0x558):
        before[offset:offset + 4] = state[offset:offset + 4]
    if before != state:
        raise Blocked("Slide transition modified unobserved state or left animation")
    for offset, original in emulator.slide_transition_before[1:]:
        if m.mem_read(DATA + offset, len(original)) != original:
            raise Blocked("Slide transition modified read-only assets")
    if m.mem_read(emulator.base + 0x880258, 8192) != emulator.slide_transition_table:
        raise Blocked("Slide transition modified the private weapon table")
    return [struct.unpack_from("<I", state, offset)[0] for offset in (0x158, 0x3c, 0x40, 0x558)]
