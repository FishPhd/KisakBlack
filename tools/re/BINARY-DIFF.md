# Whole-application compiled-code comparison

This workflow analyzes both **full native x86 EXEs**, matches their functions
with Google's BinDiff, and generates a self-contained HTML report with an
overall score and searchable function-by-function drill-down. No UI is required
to run the scan. This is separate from the small native behavioral test corpus.

## Run it

From the repository root in PowerShell:

```powershell
# Once. Installs only under ignored build-re/; does not change global PATH.
tools/re/setup.ps1 -AnalysisTools
tools/re/setup-binary-diff.ps1

# Build your desired full executable first, using an x86 VS developer prompt.
cmake --preset desktop-release
cmake --build --preset desktop-release

# Compare that exact executable with the pinned original BlackOpsMP.exe.
tools/re/re.ps1 binary-diff --candidate build-release/KisakBlack.exe

# Open the generated report in your default browser.
$result = Get-Content build-re/binary-diff/latest.json | ConvertFrom-Json
Invoke-Item $result.report
```

The scan does not rebuild the game. Any full x86 build can be supplied with
`--candidate`; differences in compiler, optimization, debug instrumentation,
platform replacements and inlining influence the score. Prefer a Release build
with matching PDB symbols beside the executable. Missing symbols do not block
comparison, but functions may appear as `FUN_<address>` and source file/line
identities cannot be inferred reliably. The scan reports signatures, names and
addresses, not invented source mappings.

To use an original installed elsewhere, put the global override **before** the
command; it must still have the pinned reference SHA-256:

```powershell
tools/re/re.ps1 --binary 'D:/Games/BlackOpsMP.exe' binary-diff --candidate build-release/KisakBlack.exe
```

First-time Ghidra analysis of each whole executable can take many minutes.
Subsequent runs reuse exports keyed by executable/PDB hashes, exporter script,
extension JARs and Ghidra version. `--refresh` forces analysis. Failed exports
remain in `build-re/binary-diff/staging/` with `analysis.log`; they are never used
as completed results. The two EXEs are analyzed, not executed.

## Reading the report

- **Overall demonstrated similarity** is the headline accuracy proxy:
  `100 × sum(original function bytes × accepted BinDiff similarity) / sum(all original function bytes)`.
  Function bytes are decoded instruction bytes, including library functions and
  thunks. Shared instructions count once per owning function. Unmatched original
  functions and matches below the confidence threshold contribute zero.
- **Original function bytes matched** shows how much of the denominator has an
  accepted candidate match. It is coverage, not accuracy.
- **Similarity within accepted matches** excludes unmatched functions. It is
  useful diagnostically, but is not the headline whole-application score.
- A function row shows BinDiff's structural similarity and match confidence
  separately. Confidence is an algorithmic score, not a calibrated probability.
  A low-confidence match retains its tentative similarity in the table while
  contributing zero to the headline. Default acceptance threshold: `0.7`.
- The detail panel includes addresses, names, signatures, decoded sizes,
  matched instruction/basic-block/edge counts, matching algorithm and weighted
  contribution. Search, sort, filter, pagination and filtered CSV export work
  directly from disk without a server or network connection.
- Candidate-only functions are available under **Unmatched candidate**. They do
  not penalize the reference-oriented headline score and are counted separately.

Change the matching threshold explicitly if needed; the chosen value is saved:

```powershell
tools/re/re.ps1 binary-diff --candidate build-release/KisakBlack.exe --min-confidence 0.9
```

Every run gets its own directory under `build-re/binary-diff/reports/` containing
`report.html`, complete `report.json`, both `.BinExport` files, the `.BinDiff`
SQLite database, the engine configuration and logs. The report records input
SHA-256 values and tool/export identities. Historical reports remain valid for
their recorded binaries, even when source files or a build change afterward.
`latest.json` is only a pointer to the most recently completed scan.

This is a **whole-EXE scan of discovered function bodies**, not a claim that all
possible code has been discovered. The report includes unique decoded function
bytes and executable-section bytes as discovery diagnostics. Data, resources,
undiscovered code and implementations in external DLLs are not scored. A 100%
structural match is not proof of identical gameplay; exact behavioral testing
remains a separate workflow. Automatic matches never update `catalog.json` or
become reviewed original-code mappings.

## Optional instruction and graph UI

The HTML function drill-down does not require BinDiff's UI. To view instruction
pairs or control-flow graphs:

1. Start the extracted engine with `--ui`:
   `build-re/vendor/bindiff/ProgramFiles/BinDiff/bin/bindiff.exe --ui`.
2. Create/open a BinDiff workspace. Choose **Diffs → Add Existing Diff**.
3. Select the `.BinDiff` database printed by `binary-diff`. Keep the adjacent
   `original.BinExport` and `candidate.BinExport` files with it.
4. Open a matched function to inspect its graph and instruction pairs.

The UI may require the official BinDiff installer if its extracted launcher
cannot locate the bundled UI/runtime. The command-line scan and HTML report do
not require that installation. Upstream: [BinDiff](https://github.com/google/bindiff),
[BinExport for Ghidra](https://github.com/google/binexport/tree/main/java).

## Setup and failures

Setup pins BinDiff 8, a specific official BinExport source commit and Gradle
8.14.3, checks archive SHA-256 values, then builds BinExport against our pinned
Ghidra 12.0.4. It uses the repository-local JDK and Gradle cache. Building the
extension downloads Maven dependencies. Windows Installer may require an
elevated PowerShell for local MSI extraction; failure reports its log rather
than silently installing system-wide. Close Ghidra before updating extensions.

`--ghidra` selects another installation of the pinned Ghidra version;
`--bindiff` or `BINDIFF_EXE` selects an engine executable. Tool hashes and the
effective BinDiff configuration are recorded even with overrides.

A successful `binary-diff` command returns exit code 0 meaning **comparison
completed**, not fidelity passed. Missing tools, mismatched architecture,
incomplete exports or invalid database identities return 2. Existing behavioral
`verify` exit codes and acceptance criteria are unchanged.

Regression tests: `tools/re/re.ps1 selftest`. They cover unmatched denominator
weighting, low-confidence handling, candidate-only code, identity validation,
invalid inventories and safe rendering of executable-controlled names.
