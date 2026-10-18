# Original-game alignment

When changing reconstructed game logic in `src/`, use the original multiplayer
binary as the behavioral reference. Read [the verification skill](tools/re/skills/original-code/SKILL.md)
and run `tools/re/re.ps1 worklist --file <repository-relative-source>` before
changing mapped logic. The workflow and acceptance limits are documented in
[tools/re/README.md](tools/re/README.md).

- Read the relevant binary mapping, calling convention, dependencies, and
  comparison coverage before editing. Decompiled C++ and Ghidra pseudocode are
  hypotheses; original instructions and independently checked execution are evidence.
- For unmapped logic, establish an evidence-backed mapping or explicitly report
  that fidelity is unverified. Empty worklists and missing tests establish no coverage.
- Rebuild the verification targets after source changes, then run affected
  comparisons. Header and tooling changes conservatively affect every pilot.
- Preserve native reference outputs and failing inputs. Never regenerate expected
  outputs from C++, relax comparisons, or label blocked/skipped cases as passes.
- Portability changes, fixes, and new features may intentionally differ. Record
  the exact scope, rationale, original behavior, and review status in the function's
  `exceptions` metadata. Keep divergences visible; approval does not turn them
  into equivalence. Ambiguous mappings and new exceptions need human review.
- Ordinary rendering/platform work remains possible within its authorized scope;
  document its relationship to original game logic rather than claiming original
  binary coverage from browser smoke tests.

Original executables, game data, memory snapshots, generated Ghidra projects,
and local captures stay under ignored `build-re/` or the existing game install.
