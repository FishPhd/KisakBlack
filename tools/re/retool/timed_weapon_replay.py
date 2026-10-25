"""Complete original continuous-fire/deploy timer and animation transitions."""
import struct

from .core import Blocked

SPECS = {
    "ContinuousFire_State_In": (0x3be610, 0x430),
    "ContinuousFire_State_Loop": (0x3be660, None),
    "ContinuousFire_State_Out": (0x3be690, 0x438),
    "Deploy_State_Deploy": (0x3beb80, 0x44c),
    "Deploy_State_Breakdown": (0x3bebd0, 0x450),
}


def validate(case):
    if (len(case) != 16 or any(not isinstance(word, int) or not 0 <= word <= 0xffffffff for word in case)
            or not 1 <= case[0] < 2048 or case[15]):
        raise ValueError("Timed weapon transition requires 16 words, nonzero weapon<2048 and zero reserved word")


def corpus(rng, random_count, name):
    if name not in SPECS:
        raise ValueError("Unknown timed weapon transition")
    values = []
    def add(weapon, mode, animation, duration):
        # Weapon, pm_type, right animation, asset duration, initial state/time/
        # delay, weapon/movement flags, left animation/state/time/delay, grenade
        # timer, waterlevel, reserved0. All non-output state stays unchanged.
        values.append((weapon, mode, animation, duration,
                       *(rng.getrandbits(32) for _ in range(11)), 0))
    for weapon in (1, 1023, 2047):
        for mode in (0, 8, 9, 10, 0xffffffff, 0x80000000, 0x7fffffff):
            for animation in (0, 0x400, 0xffff, 0xffffffff, 0x80000000, 0x12345678):
                for duration in (0, 1, 1000, 0xffffffff, 0x80000000, 0x7fffffff):
                    add(weapon, mode, animation, duration)
    for _ in range(random_count):
        add(rng.randrange(1, 2048), rng.getrandbits(32), rng.getrandbits(32), rng.getrandbits(32))
    return values


def memory_ranges(write, name):
    from .emulator import DATA
    _, duration_offset = SPECS[name]
    offsets = ([(0x3c, 8), (0x158, 4), (0x558, 4)] if write else
               [(4, 4), (0x558, 4)] + ([(0x144, 2)] if duration_offset is not None else []))
    ranges = [(DATA + 0x1000 + offset, DATA + 0x1000 + offset + size) for offset, size in offsets]
    if not write and duration_offset is not None:
        ranges += [(DATA + 0x4008, DATA + 0x400c),
                   (DATA + 0x5000 + duration_offset, DATA + 0x5004 + duration_offset)]
    return ranges


def setup(emulator, case):
    from .emulator import DATA
    validate(case)
    m, x = emulator.machine, emulator.x
    _, duration_offset = SPECS[emulator.f["name"]]
    state = bytearray(0x26e8)
    struct.pack_into("<H", state, 0x144, case[0])
    for offset, value in ((4, case[1]), (0x558, case[2]), (0x158, case[4]),
                          (0x3c, case[5]), (0x40, case[6]), (0x10, case[7]),
                          (0xc, case[8]), (0x55c, case[9]), (0x15c, case[10]),
                          (0x44, case[11]), (0x48, case[12]), (0x50, case[13]), (0x590, case[14])):
        struct.pack_into("<I", state, offset, value)
    emulator.timed_weapon_before = bytes(state)
    m.mem_write(DATA + 0x1000, bytes(state))
    emulator.timed_weapon_assets = []
    if duration_offset is not None:
        definition = bytearray(0x808)
        struct.pack_into("<I", definition, duration_offset, case[3])
        variant = bytearray(0xe4)
        struct.pack_into("<I", variant, 8, DATA + 0x5000)
        emulator.timed_weapon_assets = [(0x4000, bytes(variant)), (0x5000, bytes(definition))]
        for offset, content in emulator.timed_weapon_assets:
            m.mem_write(DATA + offset, content)
        table = bytearray(8192)
        struct.pack_into("<I", table, case[0] * 4, DATA + 0x4000)
        emulator.timed_weapon_table = bytes(table)
        m.mem_write(emulator.base + 0x880258, bytes(table))
    # Loop's original entry takes EAX, while the four asset-driven transitions
    # take ESI. The loop never binds or accesses an original weapon table.
    m.reg_write(x.UC_X86_REG_EAX if duration_offset is None else x.UC_X86_REG_ESI, DATA + 0x1000)


def outputs(emulator, case):
    from .emulator import DATA
    m = emulator.machine
    state = m.mem_read(DATA + 0x1000, 0x26e8)
    before = bytearray(emulator.timed_weapon_before)
    for offset in (0x158, 0x3c, 0x40, 0x558):
        before[offset:offset + 4] = state[offset:offset + 4]
    if before != state:
        raise Blocked("Timed weapon transition modified unobserved player-state bytes")
    for offset, original in emulator.timed_weapon_assets:
        if m.mem_read(DATA + offset, len(original)) != original:
            raise Blocked("Timed weapon transition modified read-only assets")
    if emulator.timed_weapon_assets and m.mem_read(emulator.base + 0x880258, 8192) != emulator.timed_weapon_table:
        raise Blocked("Timed weapon transition modified the weapon table")
    return [struct.unpack_from("<I", state, offset)[0] for offset in (0x158, 0x3c, 0x40, 0x558)]
