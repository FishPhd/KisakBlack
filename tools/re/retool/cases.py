"""Bit-preserving fixtures: no JSON floating point or host float conversion."""
import random
import struct

EDGE_FLOATS = [0, 0x80000000, 1, 0x80000001, 0x007FFFFF, 0x00800000,
               0x3F000000, 0xBF000000, 0x3F7FFFFF, 0x3F800000, 0x3F800001,
               0xBF800000, 0x7F7FFFFF, 0xFF7FFFFF, 0x7F800000, 0xFF800000,
               0x7FC00001, 0x7F800001, 0xFFC00001]


def corpus(name, exhaustive=False, seed=0xB10C, random_count=256):
    rng = random.Random(seed)
    if name == "DiffTrack":
        def bits(v):
            return struct.unpack("<I", struct.pack("<f", v))[0]
        values = [tuple(map(bits, v)) for v in ((2, 1, .5, .1), (.001, 0, 0, 1))]
        for target in (0, 0x80000000, 1, 0x80000001, 0x3a83126e, 0x3a83126f,
                       0x3a831270, 0xba83126e, 0xba83126f, 0xba831270, bits(1), bits(-1)):
            for current in (0., 1., -1.):
                for rate, dt in ((0., 0.), (0., 1.), (.5, .1), (1., 1.), (2., 1.), (-1., .016)):
                    values.append((target, bits(current), bits(rate), bits(dt)))
        values += [tuple(map(bits, (rng.uniform(-1e6, 1e6), rng.uniform(-1e6, 1e6),
                                  rng.uniform(-1e3, 1e3), rng.uniform(-100, 100))))
                   for _ in range(random_count)]
        return values
    if name == "G_GetClientSize":
        return [(0, 0, 0, 0)]
    if name == "BG_GetQuickScopeSpread":
        def bits(v):
            return struct.unpack("<I", struct.pack("<f", v))[0]
        values = []
        for enabled in (0, 1):
            for spread in (bits(0), bits(30) - 1, bits(30), bits(30) + 1, bits(255)):
                for scale in (bits(0), bits(1) - 1, bits(1), bits(1) + 1, bits(5)):
                    for timer in (0, 199, 200, 201, 0x80000000, 0xffffffff):
                        values.append((enabled, spread, scale, timer, bits(30), bits(.2), bits(10)))
        # Adjacent representable time settings expose multiplication rounding
        # before CVTTSS2SI; each possible adjacent integer timer is exercised.
        for time in (0, 1, bits(.001) - 1, bits(.001), bits(.001) + 1,
                     bits(.2) - 1, bits(.2), bits(.2) + 1, bits(30)):
            for timer in (0, 1, 199, 200, 201, 30000):
                values.append((1, bits(0), bits(5), timer, bits(30), time, bits(10)))
        for _ in range(random_count):
            time = bits(rng.uniform(0, 30))
            seconds = struct.unpack("<f", struct.pack("<I", time))[0]
            duration = int(struct.unpack("<f", struct.pack("<f", seconds * 1000))[0])
            values.append((rng.randrange(2), bits(rng.uniform(0, 255)), bits(rng.uniform(0, 100)),
                           (duration + rng.choice((-1, 0, 1))) & 0xffffffff,
                           bits(rng.uniform(0, 255)), time, bits(rng.uniform(0, 100))))
        for edge in EDGE_FLOATS:
            if edge & 0x7f800000 == 0x7f800000:
                continue
            for member in (1, 2):
                for multiplier in (0., 1., 255.):
                    case = [1, bits(0), bits(5), 200, bits(30), bits(.2), bits(multiplier)]
                    case[member] = edge
                    values.append(tuple(case))
        return values
    if name == "FX_GetVelocityAtTime":
        def bits(v):
            return struct.unpack("<I", struct.pack("<f", v))[0]
        values = []
        for count in (1, 2, 3, 7, 31, 127, 255):
            for flags in (0, 0x1000000, 0x2000000, 0x3000000):
                for time in (0., .25, .5, .99999994, 1.):
                    values.append((count, flags, bits(1.), bits(time), rng.randrange(256), rng.getrandbits(32),
                                   bits(10.), bits(-20.), bits(30.), rng.randrange(2)))
        values += [(rng.randrange(1, 256), rng.randrange(4) << 24, bits(1.), bits(rng.random()),
                    rng.randrange(256), rng.getrandbits(32), *[bits(rng.uniform(-50, 50)) for _ in range(3)],
                    rng.randrange(2)) for _ in range(random_count)]
        return values
    if name == "ExpandBoundsToWidth":
        def bits(v):
            return struct.unpack("<I", struct.pack("<f", v))[0]
        values = [tuple(map(bits, v)) for v in (
            (0, 0, 0, 10, 5, 2), (0, 0, 0, 2, 5, 10),
            (131072, 131072, 131072, -131072, -131072, -131072),
            (10, 0, 0, 0, 8, 2), (0, 10, 0, 8, 0, 2), (0, 0, 10, 8, 5, 0))]
        for field in range(6):
            for edge in EDGE_FLOATS:
                v = [bits(x) for x in (0, 0, 0, 10, 5, 2)]
                v[field] = edge
                values.append(tuple(v))
        values += [tuple(bits(rng.uniform(-1000, 1000)) for _ in range(6)) for _ in range(random_count)]
        return values
    if name == "FX_SetupVisualState":
        def bits(v):
            return struct.unpack("<I", struct.pack("<f", v))[0]
        values = []
        for count in range(256):
            times = [0, 0x80000000, 1, bits(.5), bits(1.) - 1, bits(1.), bits(1.) + 1, bits(1.125)]
            if count:
                for index in (1, count // 2, count):
                    if not index:
                        continue
                    boundary = bits(index / count)
                    times += [boundary - 1, boundary, boundary + 1]
            values += [(count, t, rng.getrandbits(32), 0) for t in dict.fromkeys(times)]
        values += [(rng.randrange(256), bits(rng.uniform(0, 1.125)), rng.getrandbits(32), 0)
                   for _ in range(random_count)]
        return values
    if name in ("FloatPack9S", "FloatPack10S"):
        values = EDGE_FLOATS + [rng.getrandbits(32) for _ in range(random_count)]
        # Nearby representable inputs around integer fractions expose rounding.
        for v in (-1., -.5, 0., .5, 1.):
            bits = struct.unpack("<I", struct.pack("<f", v))[0]
            values += [(bits + delta) & 0xFFFFFFFF for delta in range(-4, 5)]
        return [(v, 0, 0, 0) for v in dict.fromkeys(values)]
    if name == "Byte4UnpackRgba":
        values = [(v * 0x01010101, 0, 0, 0) for v in range(256)]
        values += [(rng.getrandbits(32), 0, 0, 0) for _ in range(random_count)]
        values += [(v[0], 1, 0, 0) for v in list(values)]
        return values
    if name == "Vec2UnpackTexCoords":
        halves = range(65536) if exhaustive else [0, 1, 0x3FFF, 0x4000, 0x7FFF, 0x8000, 0xFFFF]
        return ([(v << 16, 0, 0, 0) for v in halves] + [(v, 0, 0, 0) for v in halves] +
                [(rng.getrandbits(32), 0, 0, 0) for _ in range(random_count)])
    if name == "PM_Accelerate":
        def bits(v):
            return struct.unpack("<I", struct.pack("<f", v))[0]
        values = []
        for flags in (0, 8):
            for velocity in ((0., 0., 0.), (100., 0., 0.), (400., -20., 5.)):
                for direction in ((1., 0., 0.), (0., 0., 0.), (0.6, 0.8, 0.)):
                    for steps in (1, 8):
                        values.append((flags, *map(bits, velocity), *map(bits, direction),
                                       bits(190.), bits(9.), bits(.016), bits(100.), steps, 0, 0, 0, 0))
        for _ in range(random_count):
            values.append((rng.choice((0, 8)), *[bits(rng.uniform(-300, 300)) for _ in range(3)],
                           *[bits(rng.uniform(-1, 1)) for _ in range(3)], bits(rng.uniform(0, 300)),
                           bits(rng.choice((0., 1., 9., 1000.))), bits(rng.choice((0., .008, .016, .05))),
                           bits(rng.choice((0., 100., 300.))), rng.choice((1, 8)), 0, 0, 0, 0))
        return values
    raise ValueError(f"No corpus adapter: {name}")


def validate_quickscope(case):
    if len(case) < 7 or case[0] not in (0, 1):
        raise ValueError("Quick-scope adapter requires a boolean enable and seven input words")
    floats = [struct.unpack("<f", struct.pack("<I", case[i]))[0] for i in (1, 2, 4, 5, 6)]
    if any((case[i] & 0x7f800000) == 0x7f800000 for i in (1, 2, 4, 5, 6)):
        raise ValueError("Quick-scope adapter domain is finite state and dvar values")
    if not 0 <= floats[3] <= 30 or not 0 <= floats[2] <= 255 or not 0 <= floats[4] <= 255:
        raise ValueError("Quick-scope dvars exceed time [0,30] or spread settings [0,255]")


def validate_difftrack(case):
    if len(case) < 4:
        raise ValueError("DiffTrack requires four input words")
    for word, limit in zip(case[:4], (1e6, 1e6, 1e3, 100)):
        if word & 0x7f800000 == 0x7f800000:
            raise ValueError("DiffTrack replay requires finite inputs")
        value = struct.unpack("<f", struct.pack("<I", word))[0]
        if abs(value) > limit:
            raise ValueError("DiffTrack input outside the bounded replay domain")


def encode(cases, width=16):
    return b"".join(struct.pack(f"<{width}I", *case, *([0] * (width - len(case)))) for case in cases)


def decode_results(data, count):
    if len(data) != count * 20:
        raise ValueError(f"Runner protocol: expected {count * 20} bytes, got {len(data)}")
    result = []
    for status, *words in struct.iter_unpack("<5I", data):
        if status:
            raise ValueError(f"Native fault 0x{status:08x}")
        result.append(words)
    return result
