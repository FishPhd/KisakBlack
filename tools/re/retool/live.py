"""Bounded native gameplay capture; never substitutes a synthetic capture."""
import json
from pathlib import Path
import time
import threading

from .core import ROOT, WORK, Blocked, check_mapping, digest, reference, write_json, canonical_hash, read_json


def capture_live(f, binary=None, pid=None, seconds=30):
    import frida
    if f["name"] != "PM_Accelerate":
        raise Blocked("Live adapter currently supports PM_Accelerate only")
    path, pe = reference(binary)
    check_mapping(f, pe)
    if not 1 <= seconds <= 120:
        raise Blocked("Live capture duration must be 1..120 seconds")
    device = frida.get_local_device()
    launched = pid is None
    # Bind to loopback and request local dedicated startup. No login or remote
    # multiplayer automation is performed. An installed Steam entitlement may
    # still be required by the original executable.
    if launched:
        pid = device.spawn([str(path), "+set", "dedicated", "1", "+set", "sv_lanOnly", "1",
                            "+set", "net_ip", "127.0.0.1", "+map", "mp_nuked"], cwd=str(path.parent), stdio="pipe")
    session = None
    rows, errors, startup = [], [], []
    detached = threading.Event()
    output_dir = WORK / "live" / str(time.time_ns())
    output_dir.mkdir(parents=True)
    try:
        session = device.attach(pid)
        session.on("detached", lambda *args: detached.set())
        config = {"entry_rva": f["entry_rva"], "global_rva": f["globals"][0]["rva"], "code_size": f["code"][0]["size"]}
        source = "const CONFIG = " + json.dumps(config) + ";\n" + (ROOT / "tools/re/frida/pm_capture.js").read_text()
        script = session.create_script(source)

        def message(payload, data):
            if payload.get("type") == "error":
                errors.append(payload.get("stack", payload.get("description")))
            elif payload.get("type") == "send":
                value = payload["payload"]
                if value.get("type") == "ready":
                    try:
                        if digest(value["module"]) != digest(path):
                            errors.append("Attached process has a different executable hash")
                        import hashlib
                        loaded_hash = hashlib.sha256(bytes(value.pop("code_before_hook"))).hexdigest()
                        value["loaded_code_sha256"] = loaded_hash
                        if loaded_hash != f["code"][0]["sha256"]:
                            errors.append("Loaded function bytes differ from the pinned on-disk reference")
                    except OSError as error:
                        errors.append(str(error))
                    startup.append(value)
                elif value.get("type") == "capture" and len(rows) < 64:
                    words = value["words"]
                    record = {"input_u32": words[:16], "expected_u32": words[16:20],
                              "fpcw": words[20], "mxcsr": words[21], "registers": words[22:28],
                              "snapshot": f"snapshot-{len(rows):03d}.bin"}
                    (output_dir / record["snapshot"]).write_bytes(data)
                    record["snapshot_sha256"] = digest(output_dir / record["snapshot"])
                    rows.append(record)

        script.on("message", message)
        script.load()
        if launched:
            device.resume(pid)
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline and not detached.is_set() and not errors and len(rows) < 64:
            detached.wait(min(.25, max(0, deadline - time.monotonic())))
        if not detached.is_set():
            script.unload()
        value = {"status": "blocked" if errors or not rows else "pass", "function": f["id"],
                 "mapping": canonical_hash(f), "capture_script": digest(ROOT / "tools/re/frida/pm_capture.js"),
                 "reference": digest(path), "startup": startup, "captures": rows, "errors": errors,
                 "reason": "No gameplay calls observed; original startup/Steam or local match entry may require human action" if not rows else None,
                 "scope": "Observed native gameplay state; replay qualification is a separate live-verify operation."}
        write_json(output_dir / "manifest.json", value)
        value["manifest"] = str(output_dir / "manifest.json")
        return value
    finally:
        if session is not None:
            try:
                session.detach()
            except frida.InvalidOperationError:
                pass
        if launched:
            try:
                device.kill(pid)
            except frida.ProcessNotFoundError:
                pass


def verify_live(f, manifest_path, binary=None, wasm=False):
    from .emulator import Emulator
    from .native import execute
    from .verification import first_difference
    manifest_path = Path(manifest_path)
    manifest = read_json(manifest_path)
    if manifest.get("status") != "pass" or manifest.get("function") != f["id"] or manifest.get("mapping") != canonical_hash(f):
        raise Blocked("Live capture is blocked, stale, or belongs to another function")
    path, pe = reference(binary)
    if manifest.get("reference") != digest(path):
        raise Blocked("Live capture reference mismatch")
    cases, expected = [], []
    for row in manifest["captures"]:
        if row["fpcw"] != 0x27F or row["mxcsr"] & 0xFFC0 != 0x1F80:
            raise Blocked("Live floating-point controls differ from the qualified runner profile")
        state, stack, direction, frame = row["registers"][:4]
        velocity = state + 0x30
        if (direction < velocity + 12 and direction + 12 > velocity) or (frame + 0x24 < velocity + 12 and frame + 0x28 > velocity):
            raise Blocked("Live input aliases mutable velocity; this movement adapter requires disjoint inputs")
        snapshot = manifest_path.parent / row["snapshot"]
        if digest(snapshot) != row["snapshot_sha256"]:
            raise Blocked("Live snapshot checksum mismatch")
        data = snapshot.read_bytes()
        if len(data) != 0x26A4 * 2 + 0x8C:
            raise Blocked("Truncated live state")
        before, after = bytearray(data[:0x26A4]), data[0x26A4:0x26A4 * 2]
        before[0x30:0x3C] = after[0x30:0x3C]
        if before != after:
            raise Blocked("Live function changed undeclared player-state bytes")
        cases.append(row["input_u32"]); expected.append(row["expected_u32"])
    if not cases:
        raise Blocked("No live cases")
    em = Emulator(f, pe)
    replay = [em.execute(case) for case in cases]
    mismatch = first_difference(cases, expected, replay)
    if mismatch:
        return {"name": f["name"], "status": "mismatch", "classification": "live_emulator_replay", "mismatch": mismatch}
    replay = execute(f, cases, original=True, binary=path)
    mismatch = first_difference(cases, expected, replay)
    if mismatch:
        return {"name": f["name"], "status": "mismatch", "classification": "live_native_replay", "mismatch": mismatch}
    actual = execute(f, cases, wasm=wasm)
    mismatch = first_difference(cases, expected, actual)
    return {"name": f["name"], "status": "mismatch" if mismatch else "pass", "cases": len(cases),
            "coverage": "Live PM_Accelerate state reconstructed from all declared reads; full player-state write set checked.",
            "mismatch": mismatch, "manifest": str(manifest_path)}
