"""Complete original weapon-state pose, restricted to nonmantling fixtures."""
import struct

from .core import Blocked

VECTOR_OFFSETS = (0x20c, 0x230, 0x248, 0x26c, 0x284)


def validate(case):
    if (len(case) != 16 or any(not isinstance(v, int) or not 0 <= v <= 0xffffffff for v in case)
            or case[7] > 3 or case[9] & 4 or case[14] or case[15] > 1):
        raise ValueError("Pose requires 16 words, bool asset bits, nonmantling pm_flags and paired view")
    multiplier = struct.unpack("<f", struct.pack("<I", case[5]))[0]
    if case[5] & 0x7f800000 == 0x7f800000 or not -4 <= multiplier <= 4:
        raise ValueError("Pose recovery multiplier must be finite in [-4,4]")


def corpus(rng, random_count):
    values = []
    def add(state, time, incoming, outgoing, perks=None, multiplier=None):
        case = (state, time, incoming, outgoing,
                rng.choice((0, 0x4000, 0xffffffff)) if perks is None else perks,
                rng.choice((0, 0x80000000, 0x3f000000, 0x3f800000, 0x40000000, 0xbf800000))
                if multiplier is None else multiplier,
                rng.choice((0, 2, 0xffffffff)), rng.randrange(4), rng.getrandbits(32),
                rng.getrandbits(32) & ~4, rng.getrandbits(32), rng.getrandbits(32),
                rng.getrandbits(32), rng.getrandbits(32), 0, 0)
        values.extend((case, (*case[:15], 1)))
    for state in (*range(64), 0xffffffff, 0x80000000, 0x7fffffff):
        for time in (0, 1, 50, 100, 101, 0xffffffff):
            for incoming, outgoing in ((0, 0), (100, 0), (0, 100), (100, 100), (0xffffffff, 0x80000000)):
                add(state, time, incoming, outgoing)
    for state in (27, 28, 29, 30, 40, 41, 42, 43, 44, 47, 48, 49, 50):
        for time in (0x7fffffff, 0x80000000, 0x1000001):
            for duration in (1, 3, 0xffffffff, 0x1000001):
                for multiplier in (0, 0x80000000, 0x3f800000, 0xbf800000, 0x40800000):
                    add(state, time, duration, duration, 0x4000, multiplier)
    for _ in range(random_count):
        add(rng.choice((28, 30, 41, 43, 44, 48, 50, rng.getrandbits(32))),
            rng.getrandbits(32), rng.getrandbits(32), rng.getrandbits(32))
    return values


def pattern(seed, asset, category, component):
    """Finite normal coefficient bits, generated identically without host FP."""
    mixed = (seed ^ (0x9e3779b9 * (asset + 1)) ^ (0x45d9f3b * (category + 1))
             ^ (0x1020304 * (component + 1))) & 0xffffffff
    return (mixed & 0x80000000) | 0x3f000000 | (mixed & 0x007fffff)


def target_words(seed):
    return [((seed ^ (0x1234567 * (i + 1))) & 0x807fffff) | 0x3f800000 for i in range(3)]


def memory_ranges(write):
    from .emulator import DATA
    targets = [(DATA + 0x200, DATA + 0x20c), (DATA + 0x220, DATA + 0x22c)]
    if write:
        return targets
    offsets = ((0xc, 8), (0x3c, 4), (0x134, 4), (0x144, 2), (0x154, 8), (0x530, 4))
    ranges = targets + [(DATA + 0x1000 + a, DATA + 0x1000 + a + size) for a, size in offsets]
    ranges += [(DATA + 0x3800, DATA + 0x3804), (DATA + 0x9018, DATA + 0x901c)]
    for asset in range(3):
        ranges.append((DATA + 0x4008 + asset * 0x100, DATA + 0x400c + asset * 0x100))
        base = DATA + 0x5000 + asset * 0x1000
        ranges += [(base + a, base + a + 24) for a in VECTOR_OFFSETS]
        ranges += [(base + 0x418, base + 0x44c), (base + 0x569, base + 0x56a),
                   (base + 0x64c, base + 0x64d)]
    return ranges


def setup(emulator, case):
    from .emulator import DATA, SP
    validate(case)
    m, x = emulator.machine, emulator.x
    state = bytearray(0x26e8)
    for offset, value in ((0x158, case[0]), (0x3c, case[1]), (0x530, case[4]),
                          (0x10, case[6]), (0xc, case[9]), (0x504, case[10]),
                          (0x4fc, case[13]), (0x134, 2), (0x154, 3)):
        struct.pack_into("<I", state, offset, value)
    struct.pack_into("<H", state, 0x144, 1)
    movement = bytearray(0x90)
    struct.pack_into("<I", movement, 0, DATA + 0x1000)
    setting = bytearray(0x80)
    struct.pack_into("<I", setting, 0x18, case[5])
    fixtures = [(0x1000, bytes(state)), (0x3800, bytes(movement)), (0x9000, bytes(setting))]
    table = bytearray(8192)
    for asset in range(3):
        variant = bytearray(0xe4)
        struct.pack_into("<I", variant, 8, DATA + 0x5000 + asset * 0x1000)
        definition = bytearray(0x808)
        definition[0x569] = case[7] & 1
        definition[0x64c] = (case[7] >> 1) & 1
        for category, offset in enumerate(VECTOR_OFFSETS):
            struct.pack_into("<6I", definition, offset,
                             *(pattern(case[8], asset, category, i) for i in range(6)))
        for offset in (0x418, 0x424, 0x43c, 0x448):
            struct.pack_into("<I", definition, offset, case[2])
        for offset in (0x420, 0x42c, 0x444):
            struct.pack_into("<I", definition, offset, case[3])
        fixtures.extend(((0x4000 + asset * 0x100, bytes(variant)),
                         (0x5000 + asset * 0x1000, bytes(definition))))
        struct.pack_into("<I", table, (asset + 1) * 4, DATA + 0x4000 + asset * 0x100)
    emulator.pose_before = fixtures
    for offset, content in fixtures:
        m.mem_write(DATA + offset, content)
    emulator.pose_table = bytes(table)
    m.mem_write(emulator.base + 0x880258, bytes(table))
    m.mem_write(emulator.base + 0x863c28, struct.pack("<I", DATA + 0x9000))
    m.mem_write(DATA + 0x200, struct.pack("<3I", *target_words(case[11])))
    m.mem_write(DATA + 0x220, struct.pack("<3I", *target_words(case[12])))
    m.mem_write(SP + 4, struct.pack("<3I", DATA + 0x3800, DATA + 0x200, DATA + 0x220))


def outputs(emulator, case):
    from .emulator import DATA
    m = emulator.machine
    for offset, content in emulator.pose_before:
        if m.mem_read(DATA + offset, len(content)) != content:
            raise Blocked("Weapon pose modified a read-only fixture structure")
    if m.mem_read(emulator.base + 0x880258, 8192) != emulator.pose_table:
        raise Blocked("Weapon pose modified the original weapon table")
    if m.mem_read(emulator.base + 0x863c28, 4) != struct.pack("<I", DATA + 0x9000):
        raise Blocked("Weapon pose modified the original recovery setting pointer")
    pos = struct.unpack("<3I", m.mem_read(DATA + 0x200, 12))
    angles = struct.unpack("<3I", m.mem_read(DATA + 0x220, 12))
    return [*angles, pos[0]] if case[15] else [*pos, angles[0]]
