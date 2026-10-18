"""Bit-preserving fixtures: no JSON floating point or host float conversion."""
import random
import struct

EDGE_FLOATS = [0, 0x80000000, 1, 0x80000001, 0x007FFFFF, 0x00800000,
               0x3F000000, 0xBF000000, 0x3F7FFFFF, 0x3F800000, 0x3F800001,
               0xBF800000, 0x7F7FFFFF, 0xFF7FFFFF, 0x7F800000, 0xFF800000,
               0x7FC00001, 0x7F800001, 0xFFC00001]


def corpus(name, exhaustive=False, seed=0xB10C, random_count=256):
    rng = random.Random(seed)
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
