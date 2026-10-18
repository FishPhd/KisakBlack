"""File-level queue for branch and working-tree alignment work, never coverage."""
import time
from .core import ROOT, WORK, Blocked, functions, run, write_json
from .inventory import source_worklist


def audit_diff(base=None):
    if base is None:
        refs = run(["git", "for-each-ref", "--format=%(refname:short)", "refs/heads"], cwd=ROOT, text=True).stdout.splitlines()
        base = next((name for name in ("main", "master") if name in refs), None)
        if base is None:
            raise Blocked("Specify --base; neither local main nor master exists")
    anchor = run(["git", "merge-base", base, "HEAD"], cwd=ROOT, text=True).stdout.strip()
    paths = run(["git", "diff", "--name-only", "-z", anchor, "--"], cwd=ROOT).stdout.decode().split("\0")
    paths += run(["git", "ls-files", "--others", "--exclude-standard", "-z"], cwd=ROOT).stdout.decode().split("\0")
    mapped = functions()
    rows = []
    for path in sorted(set(p for p in paths if p)):
        units = [f for f in mapped if any(s["path"] == path for s in f["sources"])]
        symbols = {s["symbol"] for f in units for s in f["sources"] if s["path"] == path}
        exists = (ROOT / path).is_file()
        candidates = source_worklist(path) if exists and path.startswith("src/") and path.endswith(".cpp") else []
        rows.append({"path": path, "exists": exists,
                     "scope": "preserve_custom_platform_features" if path.startswith(("src/web/", "web/")) else
                              "source_requires_alignment_review" if path.startswith("src/") else "build_tooling_or_documentation",
                     "mapped_units": [{"name": f["name"], "id": f["id"], "kind": f["kind"],
                                       "mapping_status": f["mapping_status"], "coverage": f["coverage"]} for f in units],
                     "unmapped_file_definitions": [c for c in candidates if c["symbol"] not in symbols],
                     "qualification": "unverified_by_diff_inventory"})
    report = WORK / "reports" / ("diff-audit-" + str(time.time_ns()) + ".json")
    value = {"base": base, "merge_base": anchor,
             "head": run(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).stdout.strip(),
             "meaning": "Branch plus working-tree file queue. Definitions cover entire changed files, including unchanged functions; discovery is not hunk mapping or binary qualification. Preserve custom features. Read worklist/inspect and compare original instructions before core changes.",
             "files": rows}
    write_json(report, value)
    return {"base": base, "merge_base": anchor, "changed_files": len(rows),
            "source_files_requiring_review": sum(r["scope"] == "source_requires_alignment_review" for r in rows),
            "mapped_units_in_changed_files": sorted({f["name"] for r in rows for f in r["mapped_units"]}),
            "qualification": "unverified_by_diff_inventory", "report": str(report)}
