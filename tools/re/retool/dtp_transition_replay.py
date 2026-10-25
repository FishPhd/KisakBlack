"""Complete original DTP transitions with paired full-width result views."""
import struct

from .core import Blocked

SPECS = {
    "Dtp_State_In": (0x3be6e0, 0x43c, 48, 45),
    "Dtp_State_Loop": (0x3bea20, None, 49, 46),
    "Dtp_State_Out": (0x3be8c0, 0x444, 50, 47),
}


def validate(case):
    if (len(case) != 16 or any(not isinstance(v, int) or not 0 <= v <= 0xffffffff for v in case)
            or not 1 <= case[0] < 2048 or case[4] > 15 or case[7] > 15 or case[8] > 15
            or case[7] and case[7] == case[8]):
        raise ValueError("DTP requires 16 words, weapon1..2047, config bits0..15 and distinct optional clip slots0..15")


def corpus(rng, random_count, name):
    if name not in SPECS:
        raise ValueError("Unknown DTP transition")
    _, _, empty_anim, normal_anim = SPECS[name]
    values = []
    def add(mode, hand, dual, unlimited, counts, slots, animations, duration=None):
        case = (rng.choice((1, 1023, 2047)), mode, *animations,
                hand | (dual << 1) | (unlimited << 2), *counts, *slots,
                rng.getrandbits(32) if duration is None else duration,
                *(rng.getrandbits(32) for _ in range(6)))
        values.extend((case, (*case[:4], case[4] | 8, *case[5:])))
    animations = ((empty_anim, empty_anim), (normal_anim, normal_anim),
                  (empty_anim ^ 0x400, normal_anim ^ 0x400), (0xffffffff, 0x80000000))
    for mode in (0, 8, 9, 0xffffffff):
        for hand in (0, 1):
            for dual in (0, 1):
                for unlimited in (0, 1):
                    for counts in ((0, 1), (1, 0), (0, 0), (0xffffffff, 0xffffffff)):
                        for slots in ((1, 15), (8, 0), (0, 8)):
                            for pair in animations:
                                add(mode, hand, dual, unlimited, counts, slots, pair)
    for mode in (0x80000000, 0x7fffffff):
        for duration in (0, 1, 100, 0xffffffff, 0x80000000, 0x7fffffff):
            for hand in (0, 1):
                add(mode, hand, 1, 0, (0, 0), (0, 0), (0, 0x400), duration)
    for _ in range(random_count):
        slots = tuple(rng.sample(range(16), 2))
        add(rng.getrandbits(32), rng.randrange(2), rng.randrange(2), rng.randrange(2),
            (rng.getrandbits(32), rng.getrandbits(32)), slots,
            (rng.getrandbits(32), rng.getrandbits(32)))
    return values


def memory_ranges(write, name):
    from .emulator import DATA
    duration = SPECS[name][1]
    if write:
        offsets = ((0x3c, 8), (0x158, 8), (0x558, 8))
    else:
        offsets = ((4, 4), (0x61, 1), (0x144, 2), (0x558, 8))
        offsets += tuple((0x3f4 + i * 8, 8) for i in range(15))
    ranges = [(DATA + 0x1000 + offset, DATA + 0x1000 + offset + size) for offset, size in offsets]
    if not write:
        ranges += [(DATA + offset, DATA + offset + size) for offset, size in (
            (0x4008, 4), (0x4108, 4), (0x404c, 4), (0x414c, 4),
            (0x5358, 1), (0x556a, 1), (0x5594, 4))]
        if duration is not None:
            ranges.append((DATA + 0x5000 + duration, DATA + 0x5004 + duration))
    return ranges


def setup(emulator, case):
    from .emulator import DATA
    validate(case)
    m, x = emulator.machine, emulator.x
    duration = SPECS[emulator.f["name"]][1]
    state = bytearray(0x26e8)
    struct.pack_into("<H", state, 0x144, case[0])
    state[0x61] = case[4] & 1
    for offset, value in ((4, case[1]), (0x558, case[2]), (0x55c, case[3]),
                          (0x158, case[10]), (0x15c, case[11]), (0x3c, case[12]),
                          (0x40, case[13]), (0x10, case[14]), (0xc, case[15])):
        struct.pack_into("<I", state, offset, value)
    for i in range(15):
        struct.pack_into("<2I", state, 0x3f4 + i * 8, 0x10000 + i, 0x3456789)
    for slot, clip, count in ((case[7], 123, case[5]), (case[8], 456, case[6])):
        if slot:
            struct.pack_into("<2I", state, 0x3f4 + (slot - 1) * 8, clip, count)
    definition = bytearray(0x808)
    definition[0x358] = (case[4] >> 2) & 1
    definition[0x56a] = (case[4] >> 1) & 1
    left_weapon = 1 if case[0] == 2047 else 2047
    struct.pack_into("<I", definition, 0x594, left_weapon)
    if duration is not None:
        struct.pack_into("<I", definition, duration, case[9])
    fixtures = [(0x1000, bytes(state)), (0x5000, bytes(definition))]
    for offset, clip in ((0x4000, 123), (0x4100, 456)):
        variant = bytearray(0xe4)
        struct.pack_into("<I", variant, 8, DATA + 0x5000)
        struct.pack_into("<I", variant, 0x4c, clip)
        fixtures.append((offset, bytes(variant)))
    emulator.dtp_transition_before = fixtures
    for offset, content in fixtures:
        m.mem_write(DATA + offset, content)
    table = bytearray(8192)
    struct.pack_into("<I", table, case[0] * 4, DATA + 0x4000)
    struct.pack_into("<I", table, left_weapon * 4, DATA + 0x4100)
    emulator.dtp_transition_table = bytes(table)
    m.mem_write(emulator.base + 0x880258, bytes(table))
    m.reg_write(x.UC_X86_REG_ESI, DATA + 0x1000)


def outputs(emulator, case):
    from .emulator import DATA
    m = emulator.machine
    state = m.mem_read(DATA + 0x1000, 0x26e8)
    word = lambda offset: struct.unpack_from("<I", state, offset)[0]
    before = bytearray(emulator.dtp_transition_before[0][1])
    for offset in (0x158, 0x15c, 0x3c, 0x40, 0x558, 0x55c):
        before[offset:offset + 4] = state[offset:offset + 4]
    if before != state:
        raise Blocked("DTP transition modified unobserved player-state bytes")
    for offset, content in emulator.dtp_transition_before[1:]:
        if m.mem_read(DATA + offset, len(content)) != content:
            raise Blocked("DTP transition modified read-only asset fixtures")
    if m.mem_read(emulator.base + 0x880258, 8192) != emulator.dtp_transition_table:
        raise Blocked("DTP transition modified the original weapon table")
    offsets = (0x558, 0x55c, 0x158, 0x15c) if case[4] & 8 else (0x158, 0x15c, 0x3c, 0x40)
    return [word(offset) for offset in offsets]
