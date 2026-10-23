"""Build and run the production damage-dispatch regression on x86 or Wasm.

This uses explicit dependency test doubles, not an original-binary oracle.
The production function is extracted on every run; no copied implementation.
"""
import argparse
import os
import re
from pathlib import Path
import shutil
import subprocess
import struct
import sys

from retool.core import ROOT, WORK, reference, Blocked
from retool.selection import definition


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--wasm', action='store_true')
    checks = parser.add_mutually_exclusive_group()
    checks.add_argument('--check-original-layout', action='store_true',
                        help='Compile the strict original client-layout checks; a failure blocks binary-layout qualification')
    checks.add_argument('--check-original-cgame-layout', action='store_true',
                        help='Compile original snapshot, light, view and cg_s prefix offset checks; does not qualify the complete cg_s extent')
    checks.add_argument('--check-original-cgame-extent', action='store_true',
                        help='Compile the complete cg_s size guard; a prefix pass does not qualify this separate boundary')
    args = parser.parse_args()
    directory = WORK / ('damage-dispatch-wasm' if args.wasm else 'damage-dispatch-native')
    directory.mkdir(parents=True, exist_ok=True)
    generated = directory / 'selected_damage.cpp'
    fields = ROOT / 'src/qcommon/msg_mp.cpp'
    arrays = re.findall(r'^const NetField (\w+)\[', fields.read_text(encoding='utf-8'), re.M)
    if args.check_original_layout:
        _, pe = reference()
        assertions = []
        source_text = fields.read_text(encoding='utf-8')
        for name, kind, address, count in (
                ('playerStateFields', 'playerState_s', 0xacd1e0, 181),
                ('clientStateFields', 'clientState_s', 0xaccde0, 51)):
            body = re.search(r'const NetField ' + name + r'\[\d+\]\s*=\s*\{(.*?)\n\};', source_text, re.S)[1]
            actual = [(m[1], *map(int, m.group(2, 3, 4, 5))) for m in re.finditer(
                r'\{\s*"([^"]+)",\s*(-?\d+),\s*(-?\d+),\s*(-?\d+),\s*(\d+)u?', body)]
            expected = []
            for index in range(count):
                ptr, offset, size, bits, hints = struct.unpack('<IiiiI', pe.get_data(address - 0x400000 + index * 20, 20))
                field = pe.get_data(ptr - 0x400000, 160).split(b'\0', 1)[0].decode('ascii')
                if not re.fullmatch(r'[\w.\[\]]+', field):
                    raise Blocked('Invalid original field name')
                expected.append((field, offset, size, bits, hints))
                assertions.append(f'static_assert(offsetof({kind}, {field}) == {offset}, "{kind}.{field}");')
                # These character arrays are encoded in four-byte chunks;
                # their descriptor size is not sizeof(char).
                chunk = re.fullmatch(r'(name|clanAbbrev)\[(\d+)\]', field)
                if chunk:
                    member, start = chunk[1], int(chunk[2])
                    assertions.append(f'static_assert(sizeof((({kind}*)0)->{member}) >= {start + size}, "{kind}.{field} chunk extent");')
                else:
                    assertions.append(f'static_assert(sizeof((({kind}*)0)->{field}) == {size}, "{kind}.{field} width");')
            if actual != expected:
                raise Blocked(name + ': original field descriptors differ')
            print(f'{name}: {count} original field descriptors agree (names, order, offsets, sizes, encodings, hints)', flush=True)
        # Script offsets address the same restored client state. Check the
        # descriptors against the PE, independently of the C++ initializers.
        script_fields = definition(ROOT / 'src/game/g_client_fields.cpp', 'fields', data=True)
        types_text = (ROOT / 'src/game/sentient_fields.h').read_text(encoding='utf-8')
        field_types = {m[1]: int(m[2], 16) for m in re.finditer(r'(F_\w+)\s*=\s*0x([0-9A-Fa-f]+)', types_text)}
        actual_script = [(m[1], int(m[2]), int(m[3]), field_types[m[4]], int(m[5]))
                         for m in re.finditer(r'\{\s*"([^"]+)",\s*(\d+),\s*\{\s*(\d+)\s*\},\s*(F_\w+),\s*(\d+)u?', script_fields)]
        expected_script = []
        for index in range(43):
            ptr, offset, size, kind, mask, setter, getter = struct.unpack(
                '<IiiIIII', pe.get_data(0xac5878 - 0x400000 + index * 28, 28))
            if not ptr:
                if index != 42 or any((offset, size, kind, mask, setter, getter)):
                    raise Blocked('Invalid original client script-field terminator')
                break
            name = pe.get_data(ptr - 0x400000, 100).split(b'\0', 1)[0].decode('ascii')
            expected_script.append((name, offset, size, kind, mask))
        if actual_script != expected_script:
            raise Blocked('Client script-field descriptors differ from the original')
        # Nonzero offsets must also address the declared C++ member. Custom
        # accessor functions can deliberately read another field (e.g. the
        # original GetPSOffsetTime reads archiveTime); this tests the table only.
        members = {
            'maxhealth': 'sess.maxHealth', 'hasspyplane': 'hasSpyplane',
            'hassatellite': 'hasSatellite', 'disallowvehicleusage': 'disallowVehicleUsage',
            'kills': 'sess.cs.score.kills', 'deaths': 'sess.cs.score.deaths',
            'assists': 'sess.cs.score.assists', 'wagerwinnings': 'sess.cs.score.wagerWinnings',
            'killcamentity': 'sess.killCamEntity', 'killcamtargetentity': 'sess.killCamTargetEntity',
            'score': 'sess.cs.score.score', 'spectatorclient': 'sess.forceSpectatorClient',
            'archivetime': 'sess.archiveTime', 'psoffsettime': 'sess.psOffsetTime',
            'pers': 'sess.scriptPersId', 'usingturret': 'ps.eFlags', 'usingvehicle': 'ps.eFlags',
            'vehicleposition': 'ps.vehiclePos', 'divetoprone': 'ps.pm_flags',
            'sprinting': 'ps.pm_flags', 'groundentity': 'ps.groundEntityNum',
            'viewlockedentity': 'ps.viewlocked_entNum',
        }
        for name, index in (('defends', 5), ('plants', 6), ('defuses', 7), ('returns', 8),
                            ('captures', 9), ('destructions', 10), ('survived', 12),
                            ('stabs', 13), ('tomahawks', 14), ('humiliated', 15),
                            ('x2score', 16), ('headshots', 17)):
            members[name] = f'sess.scoreboardColumnCache[{index}]'
        for name, offset, size, _, _ in expected_script:
            if offset:
                member = members[name]
                assertions.append(f'static_assert(offsetof(gclient_s, {member}) == {offset}, "script {name} offset");')
                assertions.append(f'static_assert(sizeof(((gclient_s*)0)->{member}) == {size}, "script {name} width");')
        print('client script fields: 42 descriptors agree (names, order, offsets, sizes, types, masks); accessor behavior is not qualified', flush=True)
        (directory / 'original_fields.h').write_text('\n'.join(assertions) + '\n', encoding='utf-8')
    generated.write_text('#include <game_mp/g_main_mp.h>\n#include <game_mp/g_combat_mp.h>\n'
                         '#include <bgame/bg_weapons_def.h>\n#include <server_mp/sv_main_mp.h>\n#include <server/server.h>\n' +
                         definition(ROOT / 'src/game_mp/g_combat_mp.cpp', 'G_DamageClient') +
                         definition(ROOT / 'src/server_mp/sv_snapshot_mp.cpp', 'SV_FrameIsStillInArchivedSnapshotBuffer') +
                         '\n'.join(definition(fields, name, data=True) for name in arrays), encoding='utf-8')
    command = ['cmake', '-S', str(ROOT / 'tools/re/damage-dispatch'), '-B', str(directory),
               '-DGENERATED_SOURCE=' + str(generated)]
    env = dict(os.environ)
    if args.wasm:
        sdk = Path(env.get('EMSDK', 'D:/emsdk'))
        env['PATH'] = str(Path(sys.executable).parent) + os.pathsep + env.get('PATH', '')
        command += ['-G', 'Ninja', '-DCMAKE_TOOLCHAIN_FILE=' +
                    str(sdk / 'upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake')]
    else:
        command += ['-G', 'Visual Studio 17 2022', '-A', 'Win32']
    subprocess.run(command, check=True, env=env)
    target = ('damage_original_layout' if args.check_original_layout else
              'cgame_original_layout' if args.check_original_cgame_layout else
              'cgame_original_extent' if args.check_original_cgame_extent else 'damage_dispatch')
    subprocess.run(['cmake', '--build', str(directory), '--config', 'Release', '--target', target], check=True, env=env)
    if args.check_original_layout or args.check_original_cgame_layout or args.check_original_cgame_extent:
        return
    runner = [shutil.which('node') or 'node', str(directory / 'damage_dispatch.js')] if args.wasm else [str(directory / 'Release/damage_dispatch.exe')]
    subprocess.run(runner, check=True, env=env)


if __name__ == '__main__':
    main()
