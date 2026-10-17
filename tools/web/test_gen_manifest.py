"""Focused asset manifest regressions; no game install or Wasm build required."""
import argparse
import importlib.util
import json
import pathlib
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("gen_manifest", pathlib.Path(__file__).with_name("gen_manifest.py"))
manifest = importlib.util.module_from_spec(spec)
spec.loader.exec_module(manifest)


class ZombiesManifestTests(unittest.TestCase):
    def test_full_inventory_covers_untraced_archives_and_checksums(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            game, out = root / 'game', root / 'out'
            (game / 'main').mkdir(parents=True)
            (game / 'zone' / 'Common').mkdir(parents=True)
            out.mkdir()
            with manifest.zipfile.ZipFile(game / 'main' / 'iw_untraced.iwd', 'w') as archive:
                archive.writestr('images/unseen.iwi', b'unseen weapon and sound')
            (game / 'main' / 'console_mp.log').write_bytes(b'log')
            (game / 'zone' / 'Common' / 'untraced.ff').write_bytes(b'zone')
            with manifest.zipfile.ZipFile(out / 'iw_web.iwd', 'w') as archive:
                archive.writestr('images/unseen.iwi', b'unseen weapon and sound')
            m = {'boot': [{'path':'main/iw_web.iwd','size':(out / 'iw_web.iwd').stat().st_size,'gen':True}], 'maps':{}}
            inventory = manifest.write_installation_inventory(str(game), str(out), m)
            paths = {e['path']: e for e in inventory['files']}
            self.assertEqual(set(paths), {'main/iw_untraced.iwd','zone/Common/untraced.ff'})
            self.assertEqual(inventory['superseded'], ['main/iw_web.iwd'])
            self.assertEqual(paths['main/iw_untraced.iwd']['hashes'], [manifest.hashlib.sha256((game / 'main' / 'iw_untraced.iwd').read_bytes()).hexdigest()])
            self.assertEqual(m['installation']['sha256'], manifest.hashlib.sha256((out / 'installation.json').read_bytes()).hexdigest())
            original_identity = m['contentIdentity']
            manifest.write_installation_inventory(str(game), str(out), m)
            self.assertEqual(m['contentIdentity'], original_identity)
            (game / 'zone' / 'Common' / 'untraced.ff').write_bytes(b'ZONE')
            manifest.write_installation_inventory(str(game), str(out), m)
            self.assertNotEqual(m['contentIdentity'], original_identity)
            m['boot'].append({'path':'zone/Common/untraced.ff','size':99})
            with self.assertRaisesRegex(RuntimeError, 'Stale manifest'):
                manifest.write_installation_inventory(str(game), str(out), m)

    def zones(self):
        names = ["zombie_theater", "zombie_pentagon", "zombietron", *manifest.ZOMBIE_SHARED_ZONES]
        return {name: f"zone/Common/{name}.ff" for name in names}

    def test_patch_and_localized_files_do_not_supply_missing_base(self):
        zones = self.zones()
        zones.update({"zombie_moon_patch": "zone/Common/zombie_moon_patch.ff",
                      "en_zombie_moon": "zone/English/en_zombie_moon.ff"})
        audit = manifest.zombie_asset_inventory(zones)["zombie_moon"]
        self.assertEqual(audit["missing_required"], ["zombie_moon.ff"])
        maps = {}
        manifest.add_zombie_maps(zones, maps, lambda p: {"path": p, "size": 1})
        self.assertNotIn("zombie_moon", maps)

    def test_required_shared_zone_and_dead_ops_exception(self):
        zones = self.zones()
        del zones["common_zombie"]
        audit = manifest.zombie_asset_inventory(zones)
        self.assertEqual(audit["zombie_theater"]["missing_required"], ["common_zombie.ff"])
        self.assertEqual(audit["zombietron"]["missing_required"], [])

    def test_legacy_maps_require_waw_zone(self):
        zones = self.zones()
        zones["zombie_cod5_factory"] = "zone/Common/zombie_cod5_factory.ff"
        self.assertEqual(manifest.zombie_asset_inventory(zones)["zombie_cod5_factory"]["missing_required"], ["waw_zombie.ff"])

    def test_every_complete_map_is_traced_and_packed(self):
        calls = []

        def trace(args, game, name, gametype, **kwargs):
            calls.append(name)
            return {"map": name, "ok": True, "log": name + ".log", "seconds": 1, "returncode": 0}

        def collect(game, run, zones):
            if run["map"] == "frontend":
                return set(), {("main/source.iwd", "shared.iwi")}, {"frontend.cfg"}
            return {f"zone/Common/{run['map']}.ff"}, {("main/source.iwd", run["map"] + ".iwi")}, {run["map"] + ".cfg"}

        def repack(game, entries, target):
            pathlib.Path(target).write_bytes(b"test pack")
            return len(entries)

        with tempfile.TemporaryDirectory() as out, patch.object(manifest, "run_trace", side_effect=trace), \
                patch.object(manifest, "collect", side_effect=collect), patch.object(manifest, "repack_iwd", side_effect=repack):
            result = manifest.build_zombies_frontend(argparse.Namespace(), "unused", out, self.zones(), lambda p: {"path": p, "size": 1})
        self.assertEqual(set(calls), {"frontend", "zombie_theater", "zombie_pentagon", "zombietron"})
        for name, entries in result["maps"].items():
            self.assertIn(f"main/iw_web_map_{name}.iwd", [entry["path"] for entry in entries])
            self.assertIn(name + ".cfg", [entry["path"] for entry in entries])
        self.assertEqual(result["asset_inventory"]["zombie_pentagon"]["trace_status"], "passed")
        self.assertEqual(result["asset_inventory"]["zombie_moon"]["status"], "blocked by missing assets")

    def test_failed_map_trace_is_recorded_and_does_not_hide_other_maps(self):
        calls = []

        def trace(args, game, name, gametype, **kwargs):
            calls.append(name)
            return {"map": name, "ok": name != "zombie_pentagon", "log": name + ".log",
                    "seconds": 1, "returncode": 0, "log_tail": "test failure"}

        def repack(game, entries, target):
            pathlib.Path(target).write_bytes(b"test pack")
            return 0

        with tempfile.TemporaryDirectory() as out, patch.object(manifest, "run_trace", side_effect=trace), \
                patch.object(manifest, "collect", return_value=(set(), set(), set())), \
                patch.object(manifest, "repack_iwd", side_effect=repack):
            with self.assertRaises(SystemExit):
                manifest.build_zombies_frontend(argparse.Namespace(), "unused", out, self.zones(), lambda p: {"path": p, "size": 1})
            inventory = json.loads(pathlib.Path(out, "zombies-assets.json").read_text())
        self.assertEqual(set(calls), {"frontend", "zombie_theater", "zombie_pentagon", "zombietron"})
        self.assertEqual(inventory["zombie_pentagon"]["trace_status"], "failed")
        self.assertEqual(inventory["zombie_theater"]["trace_status"], "passed")


if __name__ == "__main__":
    unittest.main()
