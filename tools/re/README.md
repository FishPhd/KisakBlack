# Original-binary verification

For a full-application structural scan and interactive function drill-down, see
[Whole-application binary comparison](BINARY-DIFF.md). Run
`tools/re/re.ps1 binary-diff --candidate build-release/KisakBlack.exe` after its
one-time setup. That score measures compiled-code similarity; the workflow below
measures behavioral agreement for explicitly mapped functions.

This local Windows workflow compares selected original x86 instructions, Unicorn
execution, production MSVC x86 C++, and Emscripten Wasm. The reference is the exact
`BlackOpsMP.exe` SHA-256 recorded in `catalog.json`. A different game version needs
its own mappings; changing the hash alone is insufficient.

## Local setup and first verification

Requires Python 3.11+, VS 2022 with x86 C++/Windows SDK, and CMake 3.21+.
The Wasm target additionally uses the repository's Emscripten SDK, Ninja, and Node.
Pass `-Python C:\path\to\python.exe` if the machine's Python launcher is broken.

```powershell
tools/re/setup.ps1 -AnalysisTools
tools/re/re.ps1 doctor
tools/re/re.ps1 build
tools/re/re.ps1 index
tools/re/re.ps1 worklist --file src/universal/com_pack.cpp
tools/re/re.ps1 inspect --function FloatPack9S
tools/re/re.ps1 capture --exhaustive
tools/re/re.ps1 verify --file src/universal/com_pack.cpp
tools/re/re.ps1 selftest --mutation
```

Setup creates an isolated Python environment and optionally downloads SHA-256
checked Ghidra 12.0.4 and Temurin JDK 21.0.10+7 archives into `build-re/`. No global
installation or PATH edits are needed. `GHIDRA_HOME` can identify an existing
matching Ghidra installation; `EMSDK` identifies the Emscripten SDK. Ghidra settings,
caches, analysis logs, and projects also stay under `build-re/`. Initial analysis
uses an 8 GB heap and can take several minutes. A complete inventory is published
only after successful analysis and an explicit exporter completion record.

The executable defaults to `build/BlackOpsMP.exe`. Override it with the global
`--binary <path>` argument **before** the command, or `KISAK_REFERENCE`; its hash
must still match. Tools emit JSON. Exit codes are `0` successful requested work,
`1` behavioral mismatch, and `2` blocked/unverified work. Inventory/search success
does not claim behavioral qualification.

The movement implementation preserves the original Z/Y/X accumulation and
frame-time/speed/acceleration multiplication order. The historical mismatch in
`regressions/movement.json` remains intact and now replays successfully on MSVC
x86 and Wasm over the recorded corpus. This establishes tested output agreement,
not identical compiled instructions or live gameplay coverage.

## Work units and evidence

```powershell
tools/re/re.ps1 search --text stopspeed --limit 10
tools/re/re.ps1 search --rva 0x3b0e30
tools/re/re.ps1 search --caller-of 0x3b0e30 --limit 10
tools/re/re.ps1 index --decompile-rva 0x3b0e30
tools/re/re.ps1 inspect --function PM_Accelerate
```

The generated JSONL inventory contains discovered functions, signatures, body
ranges, callers/callees, imports, data references, and referenced strings. Query
results remain **unverified source mappings**. A requested decompilation is in
`build-re/ghidra/function-<rva>.jsonl`; the base inventory is preserved.

`catalog.json` is the maintained source-to-binary contract. Each verified record
has a hash-plus-RVA identity, possibly multiple source symbols, exact code and
constant hashes, ABI, observable outputs, coverage, dependencies, and exceptions.
Several source functions may map to the same original owner through inline
regions. File worklists aggregate mappings and list other discovered source
definitions as unverified candidates. They do not reconstruct original source
file boundaries automatically.

The four packing units include one complete original function and three inline
regions. The FloatPack regions run original arithmetic embedded in
`Vec4PackQuat`; the final integer mask is an explicitly documented output
projection. The RGBA unit stops before renderer-specific color scaling. This
coverage must not be reported as complete verification of their enclosing functions.

To add a unit: inspect original instructions and their callers, establish the ABI
and all referenced data, add the mapping and its byte hashes, then implement its
input/state adapters in the emulator, native runner, and corpus generator. Add
production dependencies to source provenance and change selection. For ambiguous
identities, retain `mapping_status: unverified` until human review resolves them.
New adapter names are intentionally unsupported until their execution paths exist.

## Captures, replay, and human verification

`capture` executes original code natively and writes content-addressed input and
expected-output records. It never uses C++ outputs as expected results. Native
execution maps only declared original regions into a private x86 process. Because
the original image's address range may collide with the runner heap, the loader
fixes validated absolute data-address operands. Internal relative branches stay
unchanged. An inline unit receives a harness return immediately after its code
range. These fixups and boundaries are recorded in each capture manifest.

Unicorn separately executes the unchanged reference bytes at their original
addresses. Undeclared code execution, reads/writes, unsupported instructions,
missing dependencies, faults, and instruction/time limits block qualification.
Registers, private memory, stack, x87 controls (`0x027f`) and MXCSR (`0x1f80`) reset
between fixtures. Observable outputs use exact bits, including packed values,
signed zero, and NaN payloads; floating-point status flags and scratch registers
are not part of this pilot's output contract.

`verify` first compares Unicorn with the native capture, then compares compiled
C++ with the same capture. Native mapping and adapter changes invalidate captures.
Production/header/tooling changes invalidate runner builds and result provenance.
Capture corruption and modified runners are rejected. `verify --changed` includes
recorded dependents; headers/build tooling conservatively select all pilots.

```powershell
tools/re/re.ps1 build --wasm
tools/re/re.ps1 verify --file src/universal/com_pack.cpp --wasm
tools/re/re.ps1 verify --function PM_Accelerate
tools/re/re.ps1 reproduce --fixture tools/re/regressions/packing.json
tools/re/re.ps1 reproduce --fixture tools/re/regressions/movement.json
tools/re/re.ps1 report
```

JSON and HTML reports are under `build-re/reports/`. Failure JSON files under
`build-re/failures/` retain the first differing case, its original expected bits,
capture identity, and provenance. Reports are historical observations of the
recorded build and inputs; rerun verification after changes. A pass establishes
agreement over that corpus and stated coverage, not universal equivalence.
Each verification returns its own report path so concurrent agent runs retain
their results. `latest.json` is an atomically published convenience snapshot;
agents should retain the specific path returned by their invocation.

Reports include exact whole-output matching and mismatching case counts and
`agreement_percent` for each unit after its emulator is qualified against the
native capture. The `accuracy` summary reports both an equal-unit mean and a
pooled case percentage. The latter can be dominated by large packing corpora.
These percentages describe the tested functions/inline regions and inputs only;
whole-game accuracy remains unknown. Blocked, unverified, and stale results do
not contribute to either percentage. Instruction/structural similarity is not
measured by these behavioral scores.

The movement pilot uses original structure offsets checked against production
headers, both flag branches, speed caps, zero direction, varying frame times,
and repeated steps. The runner checks that other player-state bytes stay unchanged.
Selected movement/normalization definitions are generated directly from production
sources at each build, with `#line` provenance; no maintained duplicate algorithms
exist. Production NetField data required by those headers is selected the same way.
Unused dependencies in the complete packing TU have terminating link guards;
reaching one blocks the run rather than supplying invented behavior.

For an actual gameplay capture, use a bounded offline original-game launch:

```powershell
tools/re/re.ps1 capture --live --function PM_Accelerate --seconds 30
# Or attach to an original game already running a local match:
tools/re/re.ps1 capture --live --function PM_Accelerate --pid 1234 --seconds 60
tools/re/re.ps1 live-verify --manifest build-re/live/<capture>/manifest.json
```

The launcher requests dedicated/local/loopback operation. It neither completes
Steam login nor navigates game menus. If startup or match entry needs human action,
start a local match manually and attach by PID. The launched test process is
terminated afterward; an attached user process is only detached. No gameplay calls
means **blocked**, not a synthetic substitute or a pass.

The Frida adapter validates the actual executable hash and loaded instruction
bytes before hooking. Native C callbacks capture floating-point controls, integer
registers, arguments, player-state snapshots, and frame state before JavaScript
processing. Live verification checks snapshot integrity and the complete player
state write set, then replays the declared reads through native original code,
Unicorn, and C++. A different floating-point control profile blocks replay until
an adapter explicitly supports it. Snapshots stay local.

## Performance and acceptance

```powershell
tools/re/re.ps1 bench --function Vec2UnpackTexCoords
tools/re/re.ps1 bench --function Vec2UnpackTexCoords --wasm
```

Benchmarks require a passing behavioral qualification. They warm the fixture set
and time 21 batches within the runner, excluding process startup, PE loading, and
I/O. Reports retain distributions, CPU/OS, source/tool provenance, and build logs.
Measurements **include adapter dispatch, floating-point initialization and argument
adaptation**; they are not isolated function latency, hardware cycle estimates, or
whole-game/browser performance. Original-vs-C++ native results share one machine;
Wasm results remain a separate backend.

`selftest` exercises failure contracts without the game. `selftest --mutation`
also builds an ignored copy of the production packing TU with a deliberately wrong
mask and requires the comparison to detect it. Production sources are checked to
remain unchanged. Exhaustive texture cases cover each 16-bit half independently;
RGBA covers every byte value, with seeded mixed-channel cases. Ordinary floating
inputs include boundaries, adjacent bit patterns, subnormals, infinities and NaNs.
RGBA fixtures include input/output aliasing and preserve that relationship in all
three runners. The movement adapter currently requires direction and frame-time
inputs disjoint from mutable velocity; live captures violating it are blocked.

The multiplayer units now include the complete no-call `BG_GetQuickScopeSpread`
routine (custom EAX input/XMM0 return, guarded state and four dvar bindings) and
the six-byte `G_GetClientSize` constant return. Their coverage and finite replay
domains are explicit in `catalog.json`. The latter also supports literal-byte
reconstruction with an explicitly supplied x86 compiler, for example
`reconstruct --function G_GetClientSize --compiler <cl.exe> --profile msvc143-sse2-precise`
from a matching x86 developer environment. The compiler/profile is recorded;
it does not establish the original build settings. A size-only ABI declaration
is checked against the compiled production type in `weapon_adapter.cpp`.

`DiffTrack` compares the complete original SSE/x87 routine with its extracted
production definition over a bounded finite corpus. It observes all 64 bits of
the double return, including precision lost by a float addition expression.
The emulator reads the physical x87 stack top with PC=53 and converts its exact
representation to double bits; unsupported precision/ranges block comparison.

`BG_GetViewmodelWeaponIndex` executes the complete original selector and its
original weapon-definition getter. The private fixture binds the original
2048-entry variant pointer table and preserves the variant-to-definition
indirection. Production structure offsets are asserted; all fixture state,
assets and table entries are checked for writes. Its raw-state coverage does
not qualify the reconstructed enum, state transitions, other asset fields or
the complete quick-scope dependency chain. Historical selector failures remain
in `regressions/viewmodel-selector.json`.
`regressions/difftrack.json` retains the original epsilon and return-precision
mismatches and can be replayed with `reproduce` on native or `--wasm`.

Intentional differences belong in scoped `exceptions` metadata with rationale,
original behavior, and review status. The current comparator remains bitwise and
does not suppress a mismatch because an exception exists. Whole-game emulation,
unrestricted dependency reconstruction, cycle simulation, renderer fidelity, and
general multithreaded replay are outside this pilot.
