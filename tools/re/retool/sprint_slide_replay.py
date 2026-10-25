"""Complete original sprint interpolation with private read-only structures.

Slide interpolation is inlined in the original pose caller and is qualified by
that caller's unit; this module does not invent a standalone slide mapping.
"""
import struct

from .core import Blocked


def validate(case):
    if len(case) < 6 or any(not isinstance(word, int) or not 0 <= word <= 0xffffffff
                            for word in case[:6]):
        raise ValueError("Sprint interpolation requires six raw uint32 words")
    if any(case[6:]):
        raise ValueError("Sprint interpolation has no additional fixture words")
    bits = case[5]
    value = struct.unpack("<f", struct.pack("<I", bits))[0]
    if bits & 0x7f800000 == 0x7f800000 or not -4 <= value <= 4:
        raise ValueError("Sprint recovery fixture requires finite multiplier in [-4,4]")


def corpus(rng, random_count):
    # state, weaponTime, sprintInTime, sprintOutTime, perks[1], multiplier bits.
    times = (0, 1, 0xffffffff, 0x80000000, 0x7fffffff,
             0x00ffffff, 0x01000000, 0x01000001)
    durations = ((0, 0), (1, 0), (0, 1), (1, 1),
                 (0xffffffff, 0x80000000), (0x7fffffff, 0x01000001))
    values = [(state, time, incoming, outgoing, 0, 0x3f800000)
              for state in (*range(64), 0xffffffff, 0x80000000, 0x7fffffff)
              for time in times for incoming, outgoing in durations]
    edges = (0, 1, 2, 3, 0xffffffff, 0xfffffffe, 0x80000000, 0x7fffffff,
             0x00ffffff, 0x01000000, 0x01000001, 0xfeffffff, 0xff000001)
    values += [(state, time, duration, duration, 0, 0x3f800000)
               for state in (27, 28, 29, 30) for time in edges for duration in edges]
    multipliers = (0xc0800000, 0xbf800000, 0x80000000, 0,
                   0x3e800000, 0x3f800000, 0x40000000, 0x40800000)
    values += [(state, time, incoming, outgoing, perks, multiplier)
               for state in (28, 30) for time in times
               for incoming, outgoing in durations for perks in (0, 0x4000, 0xffffffff)
               for multiplier in multipliers]
    for _ in range(random_count):
        # Integer quarter steps remain exact finite fixture bits, including
        # negative multipliers. Exceptional floating inputs remain out of scope.
        multiplier = struct.unpack("<I", struct.pack("<f", rng.randrange(-16, 17) / 4))[0]
        values.append((rng.choice((28, 30, rng.getrandbits(32))), rng.getrandbits(32),
                       rng.getrandbits(32), rng.getrandbits(32), rng.getrandbits(32), multiplier))
    return values


def memory_ranges(write):
    from .emulator import DATA
    if write:
        return []
    return [(DATA + 0x4000, DATA + 0x4004),
            (DATA + 0x103c, DATA + 0x1040),
            (DATA + 0x1158, DATA + 0x115c),
            (DATA + 0x1530, DATA + 0x1534),
            (DATA + 0x5418, DATA + 0x541c),
            (DATA + 0x5420, DATA + 0x5424),
            (DATA + 0x6118, DATA + 0x611c)]


def setup(emulator, case):
    from .emulator import DATA, SP
    validate(case)
    m, x = emulator.machine, emulator.x
    state = bytearray(0x26e8)
    for offset, value in ((0x158, case[0]), (0x3c, case[1]), (0x530, case[4])):
        struct.pack_into("<I", state, offset, value)
    definition = bytearray(0x808)
    struct.pack_into("<I", definition, 0x418, case[2])
    struct.pack_into("<I", definition, 0x420, case[3])
    movement = bytearray(0x90)
    struct.pack_into("<I", movement, 0, DATA + 0x1000)
    setting = bytearray(0x70)
    struct.pack_into("<I", setting, 0x18, case[5])
    emulator.sprint_before = (bytes(state), bytes(movement), bytes(definition), bytes(setting))
    for offset, content in zip((0x1000, 0x4000, 0x5000, 0x6100), emulator.sprint_before):
        m.mem_write(DATA + offset, content)
    emulator.sprint_global = struct.pack("<I", DATA + 0x6100)
    m.mem_write(emulator.base + 0x863c28, emulator.sprint_global)
    m.reg_write(x.UC_X86_REG_EAX, DATA + 0x5000)
    m.mem_write(SP + 4, struct.pack("<I", DATA + 0x4000))


def outputs(emulator, case):
    from .emulator import DATA
    m = emulator.machine
    for offset, original in zip((0x1000, 0x4000, 0x5000, 0x6100), emulator.sprint_before):
        if m.mem_read(DATA + offset, len(original)) != original:
            raise Blocked("Sprint interpolation modified read-only fixture structures")
    if m.mem_read(emulator.base + 0x863c28, 4) != emulator.sprint_global:
        raise Blocked("Sprint interpolation modified the private setting pointer")
    return [m.reg_read(emulator.x.UC_X86_REG_XMM0) & 0xffffffff, 0, 0, 0]
