---
name: original-code
description: Map and verify reconstructed KisakBlack game logic against the pinned original BlackOpsMP x86 binary before changing mapped behavior. Use for fidelity work, function reconstruction, or gameplay changes requiring original-code evidence.
---

Read [the workflow](../../README.md) for setup and contracts. All commands below
run from the repository root through `tools/re/re.ps1` and emit JSON.

1. Run `doctor`, then `worklist --file src/<subsystem>/<file>.cpp`. Unmapped
   source candidates have no verified binary identity.
2. Run `inspect --function <name>` for the relevant mapping. Use bounded
   `search --text <string>` / `search --rva <RVA>` queries and Ghidra decompilation
   when establishing a mapping. Confirm actual instructions, ABI, globals, and
   structure offsets; confidence or matching names alone cannot verify a mapping.
3. Change the production implementation. Preserve the original's integer widths,
   operation ordering, bit representations, and floating-point controls wherever
   those affect observable behavior. Inline-region coverage does not establish
   whole-function equivalence.
4. Run `build` and, for browser logic, `build --wasm`. Run `verify --function <name>`
   or `verify --changed`, then the corresponding Wasm verification. Missing native
   captures require an explicit `capture`; expected results come only from original
   execution. Do not overwrite captures just to make a mismatch disappear.
5. Retain a reproducer for each mismatch, explain the verified coverage and limits,
   and identify any blocked native gameplay capture. New intentional divergences
   need scoped exception metadata and human review. Reference results remain intact.

When adding a work unit, update the catalog and adapters together. Unsupported
dependencies or memory accesses must produce `blocked`; no silent stub results.
Use `selftest --mutation` when changing the comparison path. Readable reports are
historical observations: rerun verification after changes before claiming fidelity.
