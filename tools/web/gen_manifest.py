#!/usr/bin/env python3
"""Builds the web asset set for KisakBlack's browser dedicated server.

The browser cannot hold the install (main/ is ~6.5 GB of IWDs, zone/ another 2.5 GB), and a dedicated
server only touches a sliver of it. This tool finds that sliver by *running the engine* with
`fs_traceOpens 1` under Node (tools/web/run_node.mjs) once per map and recording every fastfile, IWD
entry and loose file it opens, then writes:

  <out>/manifest.json   boot set + one set per map (paths relative to the game directory, with sizes)
  <out>/manifest.txt    the same list as "path<TAB>size<TAB>map" lines (what the engine reads from OPFS)
  <out>/iw_web.iwd      every IWD entry any run opened, repacked into one small IWD (main/iw_web.iwd), plus a\r\n                        localized_<language>_web.iwd stub per installed language

Loading model (see web/kisak.js):
  boot set  = fastfiles every map run loaded (common_mp, code_*, patch_mp, localized pairs) + iw_web.iwd
              + loose files; imported once, kept in OPFS.
  per map   = the fastfiles only that map loaded; fetched the first time the map is played (and
              prefetched when it is next in the rotation), so a session transfers boot + played maps.

Usage:
  python tools/web/gen_manifest.py --game D:\\game --build build-web --out build-web/assets-gen
Options:
  --maps a,b          maps to trace (default: every zone/*/mp_*.ff that has a localized en_ pair)
  --gametypes tdm,... extra gametypes traced on the first map (adds IWD entries / loose files they need)
  --stage <dir>       also write a boot-only game directory (for run_node.mjs --lazy-source tests)
  --jobs N            parallel node runs (default 3)
  --node <exe>        node executable (default: node)
  --update            keep an existing manifest and add maps that are now installed (DLC copies, etc.)
"""

import argparse
import concurrent.futures as cf
import json
import hashlib
import os
import re
import subprocess
import sys
import time
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
RUNNER = os.path.join(HERE, "run_node.mjs")
TRACE_RE = re.compile(r"^FSTRACE\t(\w+)\t([^\t]*)\t(.*)$")
LOADED_RE = re.compile(r"^Loaded fastfile '([^']+)'")
# Stop when the server is actually up, or when it drops / traps. Pathnode floor
# checks and the fastfile's stored build log print "ERROR:" and then continue;
# matching those killed the trace before svs.time advanced.
SERVER_EXIT_ON = r"svs\.time=[1-9]|Com_Error|wasm trap:"
FRONTEND_EXIT_ON = r"web: status|Com_Error|wasm trap:"
FATAL_TRACE = re.compile(r"Com_Error|Com_ERROR:|wasm trap:")


def trace_failed(text):
    return FATAL_TRACE.search(text) is not None


def resolve_ci(root, rel):
    """Case-insensitive path walk; returns the on-disk relative path (true case) or None."""
    cur = root
    out = []
    for part in [p for p in rel.replace("\\", "/").split("/") if p]:
        try:
            names = os.listdir(cur)
        except OSError:
            return None
        hit = next((n for n in names if n.lower() == part.lower()), None)
        if hit is None:
            return None
        out.append(hit)
        cur = os.path.join(cur, hit)
    return "/".join(out)


def engine_rel(game, os_path):
    """'/game\\zone\\Common\\x.ff' (as the engine prints it under Node) -> on-disk relative path."""
    p = os_path.replace("\\", "/")
    if p.startswith("/game/"):
        p = p[len("/game/"):]
    return resolve_ci(game, p)


def discover_maps(game):
    zone = os.path.join(game, "zone")
    loc = set()
    for d in os.listdir(zone):
        full = os.path.join(zone, d)
        if os.path.isdir(full) and d.lower() != "common":
            loc.update(n.lower() for n in os.listdir(full))
    maps = []
    for d in os.listdir(zone):
        full = os.path.join(zone, d)
        if not os.path.isdir(full):
            continue
        for n in sorted(os.listdir(full)):
            m = re.fullmatch(r"(mp_[a-z0-9_]+)\.ff", n, re.I)
            if m and f"en_{m.group(1)}.ff".lower() in loc:
                maps.append(m.group(1).lower())
    return sorted(set(maps))


def map_fastfile_entries(map_name, zones_by_name, entry):
    """The map fastfile plus its english pair, when both are installed."""
    rows = []
    for zone in (map_name, f"en_{map_name}"):
        rel = zones_by_name.get(zone)
        if rel:
            rows.append(entry(rel))
    return rows


def missing_manifest_maps(manifest, discovered):
    have = set(manifest.get("maps", {}))
    return [name for name in discovered if name not in have]


def missing_client_maps(manifest, discovered):
    have = set((manifest.get("client") or {}).get("maps", {}))
    return [name for name in discovered if name not in have]


def merge_unique_entries(existing, added):
    seen = {e["path"].lower() for e in existing}
    out = list(existing)
    for entry in added:
        key = entry["path"].lower()
        if key in seen:
            continue
        seen.add(key)
        out.append(entry)
    return out


def add_map_fastfiles(manifest, maps, zones_by_name, entry):
    """List installed map fastfiles so Create Game can see them before client packs exist."""
    added = []
    maps_out = manifest.setdefault("maps", {})
    for name in maps:
        rows = map_fastfile_entries(name, zones_by_name, entry)
        if not rows:
            continue
        maps_out[name] = merge_unique_entries(maps_out.get(name, []), rows)
        added.append(name)
    return added


def packed_iwd_names(out_dir, filename):
    path = os.path.join(out_dir, filename)
    if not os.path.isfile(path):
        return set()
    with zipfile.ZipFile(path) as archive:
        return {n.lower().replace("\\", "/") for n in archive.namelist() if not n.endswith("/")}


def write_manifest_files(out_dir, manifest):
    manifest["generated"] = time.strftime("%Y-%m-%dT%H:%M:%S")
    with open(os.path.join(out_dir, "manifest.json"), "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=1)
    with open(os.path.join(out_dir, "manifest.txt"), "w", encoding="utf-8", newline="\n") as f:
        for e in manifest.get("boot", []):
            f.write(f"{e['path']}\t{e['size']}\t\n")
        for m, es in sorted(manifest.get("maps", {}).items()):
            for e in es:
                f.write(f"{e['path']}\t{e['size']}\t{m}\n")
        client_set = manifest.get("client")
        if client_set:
            for e in client_set.get("boot", []):
                f.write(f"{e['path']}\t{e['size']}\t\n")
            for m, es in sorted(client_set.get("maps", {}).items()):
                for e in es:
                    f.write(f"{e['path']}\t{e['size']}\t{m}\n")


def pack_additional_client_maps(args, game, out_dir, zones_by_name, manifest, maps, entry):
    """Trace and pack only the requested client maps into an existing Play set."""
    if not maps:
        return []
    if not manifest.get("client"):
        sys.exit("--update --client needs an existing client set; run a full --client generate first")
    print(f"tracing client on {len(maps)} added map(s): {', '.join(maps)}")
    runs = []
    with cf.ThreadPoolExecutor(max_workers=max(1, min(args.jobs, len(maps)))) as pool:
        futs = [pool.submit(run_trace, args, game, m, "tdm", True) for m in maps]
        for f in cf.as_completed(futs):
            r = f.result()
            print(f"  [{'ok ' if r['ok'] else 'FAIL'}] client {r['map']:<14} {len(r['entries']):5d} opens  {r['seconds']:.0f}s")
            if not r["ok"]:
                print(r["log_tail"], file=sys.stderr)
            runs.append(r)
    if not all(r["ok"] for r in runs):
        sys.exit("client trace failed (see output above)")

    server_boot = {e["path"] for e in manifest.get("boot", [])}
    server_map_zones = {e["path"] for es in manifest.get("maps", {}).values() for e in es}
    client_boot = {e["path"] for e in manifest["client"].get("boot", [])}
    already = packed_iwd_names(out_dir, IWD_NAME) | packed_iwd_names(out_dir, CLIENT_IWD_NAME)
    packed = []
    maps_out = manifest["client"].setdefault("maps", {})
    for r, zones, map_iwd, map_files in [(r, *collect(game, r, zones_by_name)) for r in runs]:
        extras = [entry(z) for z in sorted(zones - server_boot - server_map_zones - client_boot)]
        extras += [entry(f) for f in sorted(map_files)]
        needed = {(rel, name) for rel, name in map_iwd if name.lower().replace("\\", "/") not in already}
        if needed:
            pack_name = f"iw_web_map_{r['map']}.iwd"
            pack_path = os.path.join(out_dir, pack_name)
            pack_count = repack_iwd(game, needed, pack_path)
            extras.append({"path": f"main/{pack_name}", "size": os.path.getsize(pack_path), "gen": True})
            print(f"  {r['map']}: {pack_count} map-only IWD entries in {pack_name}")
        maps_out[r["map"]] = merge_unique_entries(maps_out.get(r["map"], []), extras)
        packed.append(r["map"])
    return packed


def update_existing_manifest(args, game, out_dir, zones_by_name, entry):
    """Keep the current boot/client packs and add maps that appeared on disk later (DLC)."""
    manifest_path = os.path.join(out_dir, "manifest.json")
    if not os.path.isfile(manifest_path):
        sys.exit(f"--update needs {manifest_path}; generate a full set first")
    with open(manifest_path, encoding="utf-8") as f:
        manifest = json.load(f)
    discovered = [m for m in args.maps.split(",") if m] or discover_maps(game)
    listed = add_map_fastfiles(manifest, missing_manifest_maps(manifest, discovered), zones_by_name, entry)
    write_manifest_files(out_dir, manifest)
    if listed:
        print(f"listed {len(listed)} new map(s) from zone/: {', '.join(listed)}")
    else:
        print("no new map fastfiles to list")

    packed = []
    if args.client:
        need_client = [m for m in args.client_maps.split(",") if m] if args.client_maps else missing_client_maps(manifest, discovered)
        packed = pack_additional_client_maps(args, game, out_dir, zones_by_name, manifest, need_client, entry)
        write_manifest_files(out_dir, manifest)
    print(f"updated {out_dir}")
    return {"listed": listed, "packed": packed, "manifest": manifest}


def run_trace(args, game, map_name, gametype, client=False, frontend=False):
    if frontend:
        mode = ["+set", "dedicated", "0", "+set", "com_introPlayed", "1"]
        exit_on = FRONTEND_EXIT_ON
    elif client:
        mode = ["+set", "dedicated", "0", "+devmap", map_name]
        exit_on = SERVER_EXIT_ON
    else:
        mode = ["+set", "dedicated", "1", "+map", map_name]
        exit_on = SERVER_EXIT_ON
    cmd = [
        args.node, RUNNER, "--build", args.build, "--gamedir", game,
        "--seconds", str(args.client_timeout if (client or frontend) else args.timeout),
        "--exit-on", exit_on,
        "--", "+set", "sv_maxclients", "4", "+set", "g_gametype", gametype,
        "+set", "fs_traceOpens", "1", "+set", "web_status", "1",
        *mode,
    ]
    t0 = time.time()
    proc = subprocess.run(cmd, capture_output=True, text=True, errors="replace")
    text = proc.stdout + "\n" + proc.stderr
    log_dir = getattr(args, "trace_log_dir", "")
    log_path = None
    if log_dir:
        os.makedirs(log_dir, exist_ok=True)
        mode_name = "frontend" if frontend else "client" if client else "server"
        log_path = os.path.join(log_dir, f"{map_name}-{gametype}-{mode_name}.log")
        with open(log_path, "w", encoding="utf-8") as f:
            f.write("command: " + json.dumps(cmd) + "\n")
            f.write(text)
    entries = []
    for line in text.splitlines():
        line = line.rstrip("\r")
        m = TRACE_RE.match(line)
        if m:
            entries.append((m.group(1), m.group(2), m.group(3)))
            continue
        # The first fastfiles load before fs_traceOpens exists; the load log still names them.
        m = LOADED_RE.match(line)
        if m:
            entries.append(("zone", m.group(1), ""))
    ready = r"web: status" if frontend else r"svs\.time=[1-9]"
    ok = re.search(ready, text) is not None
    failed = trace_failed(text)
    return {
        "map": map_name, "gametype": gametype, "client": client, "entries": entries,
        "ok": ok and not failed and proc.returncode == 0,
        "seconds": time.time() - t0, "returncode": proc.returncode, "log": log_path,
        "log_tail": "\n".join(text.splitlines()[-15:]),
    }


def zone_index(game):
    """lower-case zone name -> relative path of its .ff (zone/<dir>/<name>.ff)"""
    index = {}
    zone = os.path.join(game, "zone")
    for d in os.listdir(zone):
        full = os.path.join(zone, d)
        if os.path.isdir(full):
            for n in os.listdir(full):
                if n.lower().endswith(".ff"):
                    index[n[:-3].lower()] = f"zone/{d}/{n}"
    return index


IWD_NAME = "iw_web.iwd"


def installed_languages(game):
    main = resolve_ci(game, "main")
    langs = []
    for n in sorted(os.listdir(os.path.join(game, main))) if main else []:
        m = re.match(r"localized_([A-Za-z]+)_", n)
        if m and m.group(1) not in langs:
            langs.append(m.group(1))
    return langs


# Opened before the dvar system (and so fs_traceOpens) exists: Win_InitLocalization reads the language
# from here, and without it every zone path comes out as zone\(null)\...
ALWAYS_FILES = ("localization.txt",)

# Profile/user data the engine also opens; it is created fresh in the browser, never shipped.
SKIP_FILES = ("players/", "profile/", "usermaps/", "mods/")


def collect(game, run, zones_by_name):
    """-> (zones {rel}, iwd {(iwd_rel, entry_name)}, files {rel})"""
    zones, iwd, files = set(), set(), set()
    for kind, name, source in run["entries"]:
        if kind == "zone":
            rel = zones_by_name.get(name.lower()) or (engine_rel(game, source) if source else None)
        else:
            rel = engine_rel(game, source)
        if rel is None:
            # an optional zone that is not installed (dev_mp, ...) is not an error
            if kind != "zone":
                print(f"  ! cannot resolve {kind} {name} <- {source}", file=sys.stderr)
            continue
        if kind == "zone":
            zones.add(rel)
        elif kind == "iwd":
            iwd.add((rel, name))
        elif kind == "file":
            if not rel.lower().startswith(SKIP_FILES):
                files.add(rel)
    return zones, iwd, files


def repack_iwd(game, entries, out_path):
    """Copy the given (iwd, entry) pairs into one IWD (a zip)."""
    by_iwd = {}
    for iwd_rel, name in entries:
        by_iwd.setdefault(iwd_rel, []).append(name)
    written = set()
    count = 0
    with zipfile.ZipFile(out_path, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as out:
        for iwd_rel in sorted(by_iwd):
            with zipfile.ZipFile(os.path.join(game, iwd_rel)) as src:
                index = {n.lower().replace("\\", "/"): n for n in src.namelist()}
                for name in sorted(by_iwd[iwd_rel]):
                    key = name.lower().replace("\\", "/")
                    real = index.get(key)
                    if real is None:
                        print(f"  ! {iwd_rel} has no entry {name}", file=sys.stderr)
                        continue
                    if key in written:  # an earlier IWD in the search order wins, like the engine
                        continue
                    info = src.getinfo(real)
                    data = src.read(real)
                    zi = zipfile.ZipInfo(real, date_time=info.date_time)
                    zi.compress_type = zipfile.ZIP_DEFLATED
                    out.writestr(zi, data)
                    written.add(key)
                    count += 1
    return count


def stage_game(game, out_dir, stage_dir, boot):
    """A game directory holding only the boot set (+ manifest.txt): what a fresh browser profile has
    after its first import. Used with run_node.mjs --lazy-source to test per-map loading headlessly."""
    import shutil
    for e in boot:
        src = os.path.join(out_dir, os.path.basename(e["path"])) if e.get("gen") else os.path.join(game, e["path"])
        dst = os.path.join(stage_dir, e["path"])
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        if os.path.exists(dst) and os.path.getsize(dst) == e["size"]:
            continue
        shutil.copyfile(src, dst)
    shutil.copyfile(os.path.join(out_dir, "manifest.txt"), os.path.join(stage_dir, "manifest.txt"))
    print(f"staged boot set in {stage_dir}")


CLIENT_IWD_NAME = "iw_web_client.iwd"


def build_client_set(args, game, out_dir, zones_by_name, server_boot_zones, server_maps, server_iwd, server_files, entry):
    """Trace the engine as a client (null GL under Node) and return {"boot": [...], "maps": {...}}:
    what the Play button needs on top of the server set (ui/gfx fastfiles, images from IWDs, loose files)."""
    print("tracing client frontend (dedicated 0, no map) ...")
    front = run_trace(args, game, "frontend", "tdm", client=True, frontend=True)
    print(f"  [{'ok ' if front['ok'] else 'FAIL'}] frontend       {len(front['entries']):5d} opens  {front['seconds']:.0f}s")
    if not front["ok"]:
        print(front["log_tail"], file=sys.stderr)
        sys.exit("frontend trace failed (see output above)")

    cmaps = [m for m in args.client_maps.split(",") if m]
    print(f"tracing client on {len(cmaps)} maps: {', '.join(cmaps)}")
    runs = []
    with cf.ThreadPoolExecutor(max_workers=max(1, min(args.jobs, len(cmaps)))) as pool:
        futs = [pool.submit(run_trace, args, game, m, "tdm", True) for m in cmaps]
        for f in cf.as_completed(futs):
            r = f.result()
            print(f"  [{'ok ' if r['ok'] else 'FAIL'}] client {r['map']:<14} {len(r['entries']):5d} opens  {r['seconds']:.0f}s")
            if not r["ok"]:
                print(r["log_tail"], file=sys.stderr)
            runs.append(r)
    if not all(r["ok"] for r in runs):
        sys.exit("client trace failed (see output above)")

    runs.append(front)
    per_run = [(r, *collect(game, r, zones_by_name)) for r in runs]
    server_map_zones = {e["path"] for es in server_maps.values() for e in es}
    map_only = [x for x in per_run if x[0]["map"] != "frontend"]
    front_only = [x for x in per_run if x[0]["map"] == "frontend"]
    # Map traces share the in-game client set. The frontend trace adds the main-menu zones on top
    # (they are not an intersection: a devmap run never opens the idle lobby).
    boot_zones = (set.intersection(*[x[1] for x in map_only]) if map_only else set())
    if front_only:
        boot_zones |= front_only[0][1]
    boot_zones -= server_boot_zones | server_map_zones
    front_iwd = (front_only[0][2] if front_only else set()) - server_iwd
    files = set().union(*[x[3] for x in per_run]) - server_files

    boot = [entry(z) for z in sorted(boot_zones)] + [entry(f) for f in sorted(files)]
    maps_out = {}
    for r, zones, _, _ in per_run:
        if r.get("map") == "frontend":
            continue
        have = {e["path"] for e in server_maps.get(r["map"], [])}
        maps_out[r["map"]] = [entry(z) for z in sorted(zones - server_boot_zones - boot_zones - have)]

    iwd_path = os.path.join(out_dir, CLIENT_IWD_NAME)
    print(f"repacking {len(front_iwd)} frontend IWD entries ...")
    n = repack_iwd(game, front_iwd, iwd_path)
    boot.append({"path": f"main/{CLIENT_IWD_NAME}", "size": os.path.getsize(iwd_path), "gen": True})
    for r, _, map_iwd, _ in map_only:
        map_name = r["map"]
        needed = map_iwd - front_iwd - server_iwd
        if not needed:
            continue
        pack_name = f"iw_web_map_{map_name}.iwd"
        pack_path = os.path.join(out_dir, pack_name)
        pack_count = repack_iwd(game, needed, pack_path)
        maps_out[map_name].append({"path": f"main/{pack_name}", "size": os.path.getsize(pack_path), "gen": True})
        print(f"  {map_name}: {pack_count} map-only IWD entries in {pack_name}")
    mb = lambda b: b / 1048576
    print(f"client boot: {len(boot)} files, {mb(sum(e['size'] for e in boot)):.1f} MB ({n} IWD entries)")
    for m, es in sorted(maps_out.items()):
        print(f"  {m:<18} +{len(es)} files, {mb(sum(e['size'] for e in es)):.1f} MB")
    return {"boot": boot, "maps": maps_out}


def write_installation_inventory(game, out_dir, manifest):
    """Inventory the installed asset universe, independently of trace coverage.

    Original IWDs supply every archive member, including sounds/images never
    exercised by a capture. Videos/executables/logs/profiles are not engine assets
    used by the web client. Hash blocks allow bounded-memory browser validation.
    """
    block_size = 4 * 1024 * 1024
    files = {}
    asset_extensions = {'.iwd', '.ff', '.iwi', '.wav', '.mp3', '.ogg', '.csv', '.cfg',
                        '.menu', '.txt', '.arena', '.str', '.gsc', '.csc', '.atr',
                        '.bin', '.d3dbsp', '.sun', '.vision', '.hlsl', '.techset'}
    for base in ('main', 'zone'):
        directory = resolve_ci(game, base)
        if not directory:
            continue
        for root, dirs, names in os.walk(os.path.join(game, directory), followlinks=False):
            dirs[:] = sorted(d for d in dirs if not os.path.islink(os.path.join(root, d)))
            for name in sorted(names):
                source = os.path.join(root, name)
                if os.path.islink(source) or os.path.splitext(name)[1].lower() not in asset_extensions:
                    continue
                rel = os.path.relpath(source, game).replace('\\', '/')
                files[rel.lower()] = ({'path': rel, 'size': os.path.getsize(source)}, source)
    entries = [*manifest['boot'], *sum(manifest.get('maps', {}).values(), [])]
    for mode in ('client',):
        group = manifest.get(mode, {})
        entries += group.get('boot', []) + sum(group.get('maps', {}).values(), [])
    superseded = []
    original_members = {}
    original_names = {e['path'].lower() for e, _ in files.values() if e['path'].lower().endswith('.iwd')}
    for entry, source in sorted(files.values(), key=lambda pair: pair[0]['path'].lower()):
        if not source.lower().endswith('.iwd'):
            continue
        with zipfile.ZipFile(source) as archive:
            for member in archive.infolist():
                original_members.setdefault(member.filename.lower().replace('\\', '/'), (member.CRC, member.file_size))
    for entry in entries:
        # Trace packs are subsets of the original archives. Installing both
        # duplicates gigabytes and can exceed an origin's quota needlessly.
        source = os.path.join(out_dir, os.path.basename(entry['path'])) if entry.get('gen') else os.path.join(game, entry['path'])
        if original_names and entry.get('gen') and entry['path'].lower().endswith('.iwd'):
            name = os.path.basename(entry['path']).lower()
            localized_stub = name.startswith('localized_') and name.endswith('_web.iwd')
            language_prefix = 'main/' + name.removesuffix('_web.iwd') + '_'
            with zipfile.ZipFile(source) as archive:
                covered = all(original_members.get(m.filename.lower().replace('\\', '/')) == (m.CRC, m.file_size)
                              for m in archive.infolist() if not m.is_dir())
            if covered or (localized_stub and any(n.startswith(language_prefix) for n in original_names)):
                superseded.append(entry['path'])
                continue
        files[entry['path'].lower()] = (dict(entry), source)
    result = []
    for entry, source in sorted(files.values(), key=lambda pair: pair[0]['path'].lower()):
        before = os.stat(source)
        hashes = []
        with open(source, 'rb') as stream:
            while data := stream.read(block_size):
                hashes.append(hashlib.sha256(data).hexdigest())
        after = os.stat(source)
        if (before.st_size, before.st_mtime_ns) != (after.st_size, after.st_mtime_ns):
            raise RuntimeError(f'Asset changed while hashing: {source}')
        if entry['size'] != before.st_size:
            raise RuntimeError(f'Stale manifest size: {source}; regenerate the asset packs')
        result.append({**entry, 'blockSize': block_size, 'hashes': hashes})
    # Unlike inventory metadata, this identity changes only with asset bytes.
    # Same-size FF/IWD replacements must invalidate portable shader catalogs.
    content = [{'path': e['path'].lower(), 'size': e['size'], 'hashes': e['hashes']} for e in result]
    manifest['contentIdentity'] = hashlib.sha256(json.dumps(content, sort_keys=True, separators=(',', ':')).encode('utf-8')).hexdigest()
    inventory = {'version': 1, 'scope': 'installed-web-assets',
                 'excluded': ['videos (web client has no Bink playback)', 'executables', 'logs', 'player profiles'],
                 'files': result, 'superseded': sorted(set(superseded)), 'bytes': sum(e['size'] for e in result)}
    data = json.dumps(inventory, indent=1).encode('utf-8')
    inventory_path = os.path.join(out_dir, 'installation.json')
    with open(inventory_path + '.tmp', 'wb') as stream:
        stream.write(data)
    os.replace(inventory_path + '.tmp', inventory_path)
    manifest['installation'] = {'version': 1, 'path': 'installation.json', 'size': len(data),
                                'sha256': hashlib.sha256(data).hexdigest()}
    print(f"full installation: {len(result)} files, {inventory['bytes'] / 1048576:.1f} MB")
    return inventory


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--game", required=True, help="game directory (main/, zone/)")
    ap.add_argument("--build", default="build-web", help="directory with kisak.mjs / kisak.wasm")
    ap.add_argument("--out", default=None, help="output directory (default: <build>/assets-gen)")
    ap.add_argument("--maps", default="", help="comma list (default: discover)")
    ap.add_argument("--gametypes", default="", help="extra gametypes to trace on the first map")
    ap.add_argument("--stage", default="", help="also write a boot-only game directory here (headless tests)")
    ap.add_argument("--jobs", type=int, default=3)
    ap.add_argument("--timeout", type=int, default=240, help="seconds per traced run")
    ap.add_argument("--node", default="node")
    ap.add_argument("--gpu-catalog-session", default="", help="after packing assets, capture a WebGPU recipe catalog using this owned headed CDP session")
    ap.add_argument("--inventory-only", action="store_true", help="add/update the complete hashed installation inventory without rerunning traces")
    ap.add_argument("--update", action="store_true",
                    help="keep the existing manifest and add maps now present in zone/ (DLC copies)")
    ap.add_argument("--client", action="store_true",
                    help="also trace the *client* (dedicated 0, +devmap) and add a client set for the Play button")
    ap.add_argument("--client-maps", default=None,
                    help="maps to trace as a client; defaults to every discovered multiplayer map")
    ap.add_argument("--client-timeout", type=int, default=900, help="seconds per traced client run")
    args = ap.parse_args()

    game = os.path.abspath(args.game)
    args.build = os.path.abspath(args.build)
    out_dir = os.path.abspath(args.out or os.path.join(args.build, "assets-gen"))
    os.makedirs(out_dir, exist_ok=True)
    if args.inventory_only:
        manifest_path = os.path.join(out_dir, 'manifest.json')
        with open(manifest_path, encoding='utf-8') as stream:
            manifest = json.load(stream)
        write_installation_inventory(game, out_dir, manifest)
        with open(manifest_path + '.tmp', 'w', encoding='utf-8') as stream:
            json.dump(manifest, stream, indent=1)
        os.replace(manifest_path + '.tmp', manifest_path)
        return
    # Every invocation retains its traces; later attempts must not overwrite failure evidence.
    args.trace_log_dir = os.path.join(out_dir, "traces", time.strftime("%Y%m%dT%H%M%S") + f"-{time.time_ns()}")
    zones_by_name = zone_index(game)

    def entry(rel, **extra):
        e = {"path": rel, "size": os.path.getsize(os.path.join(game, rel))}
        e.update(extra)
        return e

    if args.update:
        update_existing_manifest(args, game, out_dir, zones_by_name, entry)
        return

    maps = [m for m in args.maps.split(",") if m] or discover_maps(game)
    if args.client and args.client_maps is None:
        # The stock map picker exposes every discovered MP map. Trace their client
        # dependencies too; a server-only fastfile entry is not enough for the
        # map-specific IWD assets (FX, HUD/materials, minimap, and killstreaks).
        args.client_maps = ",".join(discover_maps(game))
    extra_gt = [g for g in args.gametypes.split(",") if g and g != "tdm"]
    print(f"tracing {len(maps)} maps: {', '.join(maps)}")

    jobs = [(m, "tdm") for m in maps] + [(maps[0], g) for g in extra_gt]
    results = []
    with cf.ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
        futs = {pool.submit(run_trace, args, game, m, g): (m, g) for m, g in jobs}
        for f in cf.as_completed(futs):
            r = f.result()
            tag = "ok " if r["ok"] else "FAIL"
            print(f"  [{tag}] {r['map']:<18} {r['gametype']:<6} {len(r['entries']):5d} opens  {r['seconds']:.0f}s")
            if not r["ok"]:
                print(r["log_tail"], file=sys.stderr)
            results.append(r)
    if not all(r["ok"] for r in results):
        sys.exit("trace failed (see output above)")

    per_run = [(r, *collect(game, r, zones_by_name)) for r in results]
    map_runs = [x for x in per_run if x[0]["gametype"] == "tdm"]

    # boot = fastfiles that every map loaded; per map = the rest of that map's fastfiles
    boot_zones = set.intersection(*[x[1] for x in map_runs])
    all_iwd = set().union(*[x[2] for x in per_run])
    all_files = set().union(*[x[3] for x in per_run])
    for name in ALWAYS_FILES:
        rel = resolve_ci(game, name)
        if rel:
            all_files.add(rel)
        else:
            print(f"  ! required file {name} not found in {game}", file=sys.stderr)

    boot = [entry(z) for z in sorted(boot_zones)] + [entry(f) for f in sorted(all_files)]
    maps_out = {}
    for r, zones, _, _ in map_runs:
        maps_out[r["map"]] = [entry(z) for z in sorted(zones - boot_zones)]

    # Generated IWDs. The engine only accepts iw_*.iwd in main/ (pure check) ...
    iwd_path = os.path.join(out_dir, IWD_NAME)
    print(f"repacking {len(all_iwd)} IWD entries ...")
    n = repack_iwd(game, all_iwd, iwd_path)
    boot.append({"path": f"main/{IWD_NAME}", "size": os.path.getsize(iwd_path), "gen": True})
    # ... and a language only counts as installed if a localized_<language>_*.iwd is present. The real
    # ones are gigabytes of speech; nothing a dedicated server opens lives in them (the traces show it),
    # so a one-entry stub per installed language keeps the language logic happy.
    for lang in installed_languages(game):
        stub = f"localized_{lang}_web.iwd"
        stub_path = os.path.join(out_dir, stub)
        with zipfile.ZipFile(stub_path, "w", zipfile.ZIP_STORED) as z:
            z.writestr("localized_stub.txt", "placeholder: marks the language as installed\n")
        boot.append({"path": f"main/{stub}", "size": os.path.getsize(stub_path), "gen": True})

    client_out = None
    if args.client:
        client_out = build_client_set(args, game, out_dir, zones_by_name, boot_zones, maps_out, all_iwd, all_files, entry)

    manifest = {
        "version": 1,
        "generated": time.strftime("%Y-%m-%dT%H:%M:%S"),
        "boot": boot,
        "maps": maps_out,
    }
    if client_out:
        manifest["client"] = client_out
    write_installation_inventory(game, out_dir, manifest)
    with open(os.path.join(out_dir, "manifest.json"), "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=1)
    with open(os.path.join(out_dir, "manifest.txt"), "w", encoding="utf-8", newline="\n") as f:
        for e in boot:
            f.write(f"{e['path']}\t{e['size']}\t\n")
        for m, es in sorted(maps_out.items()):
            for e in es:
                f.write(f"{e['path']}\t{e['size']}\t{m}\n")
        client_set = manifest.get("client")
        if client_set:
            for e in client_set["boot"]:
                f.write(f"{e['path']}\t{e['size']}\t\n")
            for m, es in sorted(client_set["maps"].items()):
                for e in es:
                    f.write(f"{e['path']}\t{e['size']}\t{m}\n")

    if args.stage:
        stage_dir = os.path.abspath(args.stage)
        stage_game(game, out_dir, stage_dir, boot)

    mb = lambda b: b / 1048576
    boot_mb = mb(sum(e["size"] for e in boot))
    print(f"boot set: {len(boot)} files, {boot_mb:.1f} MB ({IWD_NAME} {mb(os.path.getsize(iwd_path)):.2f} MB, {n} entries)")
    for m, es in sorted(maps_out.items()):
        print(f"  {m:<18} +{len(es)} files, {mb(sum(e['size'] for e in es)):.1f} MB")
    print(f"wrote {out_dir}")
    if args.gpu_catalog_session:
        subprocess.run([args.node, os.path.join(os.path.dirname(__file__), "generate_gpu_catalog.mjs"),
                        "--session", os.path.abspath(args.gpu_catalog_session), "--out", out_dir], check=True)


if __name__ == "__main__":
    main()
