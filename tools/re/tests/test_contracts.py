import copy
import hashlib
import struct
import tempfile
from pathlib import Path
from types import SimpleNamespace
import unittest
from unittest.mock import patch

from retool import core
from retool.cases import corpus, encode, decode_results, validate_quickscope
from retool.emulator import Emulator, DATA
from retool.selection import definition
from retool.verification import first_difference, load_capture
from retool import native, verification
from retool.cli import main
import io
from contextlib import redirect_stdout


class TinyPE:
    OPTIONAL_HEADER = SimpleNamespace(ImageBase=0x400000)

    def __init__(self, code):
        self.image = bytearray(0x3000)
        self.image[0x1000:0x1000 + len(code)] = code

    def get_data(self, rva, size):
        return bytes(self.image[rva:rva + size])


def program(code):
    return {"name": "Byte4UnpackRgba", "entry_rva": 0x1000, "stop_rva": None,
            "mapping_status": "verified", "output_words": 4,
            "code": [{"rva": 0x1000, "size": len(code), "sha256": hashlib.sha256(code).hexdigest()}]}


class Contracts(unittest.TestCase):
    def test_selected_definition_excludes_control_flow_calls(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "sample.cpp"
            path.write_text("void caller() {\n    if (target(1)) { }\n}\n"
                            "bool target(int value) { return value > 0; }\n")
            selected = definition(path, "target")
            self.assertIn("bool target(int value)", selected)
            self.assertNotIn("if (target", selected)

    def test_quickscope_domain_and_boundary_corpus(self):
        cases = corpus("BG_GetQuickScopeSpread")
        for case in cases:
            validate_quickscope(case)
        for member, value in ((0, 2), (2, 0x7fc00001), (5, 0x7f800000), (5, 0x42000000)):
            case = list(cases[0]); case[member] = value
            with self.assertRaises(ValueError):
                validate_quickscope(case)
        self.assertEqual(corpus("G_GetClientSize"), [(0, 0, 0, 0)])

    def test_quickscope_original_writes_are_blocked(self):
        # A synthetic bad original attempting a player-state write must be
        # rejected before its returned value could qualify a comparison.
        code = b"\x89\x90\xec\x04\x00\x00\xc3"
        f = program(code); f["name"] = "BG_GetQuickScopeSpread"; f["globals"] = []
        for rva in (0x84f874, 0x84f928, 0x851a8c, 0x8519e0):
            f["globals"].append(dict(rva=rva, size=4, sha256=hashlib.sha256(bytes(4)).hexdigest()))
        pe = TinyPE(code)
        original_get = pe.get_data
        pe.get_data = lambda rva, size: original_get(rva, size).ljust(size, b"\0")
        with self.assertRaisesRegex(core.Blocked, "Undeclared write"):
            Emulator(f, pe).execute(corpus("BG_GetQuickScopeSpread")[0])

    def test_wrong_reference_is_rejected_before_loading(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "wrong.exe"; path.write_bytes(b"wrong")
            with self.assertRaisesRegex(core.Blocked, "Wrong reference"):
                core.reference(path)

    def test_missing_reference_is_blocked(self):
        with self.assertRaisesRegex(core.Blocked, "not found"):
            core.reference(core.WORK / "does-not-exist.exe")

    def test_duplicate_mapping_is_rejected(self):
        catalog = copy.deepcopy(core.load_catalog())
        catalog["functions"].append(catalog["functions"][0])
        with patch.object(core, "read_json", return_value=catalog):
            with self.assertRaisesRegex(core.Blocked, "Duplicate"):
                core.load_catalog()

    def test_unsupported_tolerance_is_rejected(self):
        catalog = copy.deepcopy(core.load_catalog()); catalog["functions"][0]["comparison"] = "close_enough"
        with patch.object(core, "read_json", return_value=catalog):
            with self.assertRaisesRegex(core.Blocked, "Unsupported comparison"):
                core.load_catalog()

    def test_unverified_mapping_never_executes(self):
        f = program(b"\xc3"); f["mapping_status"] = "unverified"
        with self.assertRaisesRegex(core.Blocked, "unverified"):
            Emulator(f, TinyPE(b"\xc3"))

    def test_stale_instruction_hash_is_rejected(self):
        with self.assertRaisesRegex(core.Blocked, "stale bytes"):
            Emulator(program(b"\xc3"), TinyPE(b"\x90"))

    def test_zero_filled_bss_has_memory_identity(self):
        section = SimpleNamespace(VirtualAddress=0x1000, Misc_VirtualSize=4096, SizeOfRawData=0)
        pe = SimpleNamespace(get_data=lambda rva, size: b"", get_section_by_rva=lambda rva: section)
        self.assertEqual(core.image_bytes(pe, 0x1000, 4), bytes(4))
        self.assertEqual(core.image_bytes(pe, 0x1fff, 4), b"")

    def test_undeclared_read_in_mapped_page_is_rejected(self):
        code = b"\xa1" + struct.pack("<I", 0x401100) + b"\xc3"
        with self.assertRaisesRegex(core.Blocked, "Undeclared read"):
            Emulator(program(code), TinyPE(code)).execute((0, 0, 0, 0))

    def test_unmapped_read_is_rejected(self):
        code = b"\xa1" + struct.pack("<I", 0x12340000) + b"\xc3"
        with self.assertRaisesRegex(core.Blocked, "Emulator fault"):
            Emulator(program(code), TinyPE(code)).execute((0, 0, 0, 0))

    def test_undeclared_input_write_is_rejected(self):
        code = b"\x89\x01\xc3"  # mov [ecx],eax
        with self.assertRaisesRegex(core.Blocked, "Undeclared write"):
            Emulator(program(code), TinyPE(code)).execute((0, 0, 0, 0))

    def test_undeclared_instruction_in_mapped_page_is_rejected(self):
        code = b"\xeb\x02"  # jump beyond the declared two-byte range
        with self.assertRaisesRegex(core.Blocked, "Undeclared instruction"):
            Emulator(program(code), TinyPE(code)).execute((0, 0, 0, 0))

    def test_infinite_loop_is_bounded(self):
        code = b"\xeb\xfe"
        with self.assertRaisesRegex(core.Blocked, "limit"):
            Emulator(program(code), TinyPE(code)).execute((0, 0, 0, 0))

    def test_memory_is_reset_between_cases(self):
        code = b"\x89\x08\xc3"  # mov [eax],ecx (declared output)
        em = Emulator(program(code), TinyPE(code))
        self.assertEqual(em.execute((1, 0, 0, 0))[0], DATA)
        em.machine.mem_write(DATA + 0x104, b"\xff" * 4)
        self.assertEqual(em.execute((2, 0, 0, 0))[1], 0)

    def test_bit_protocol_preserves_nan_payload(self):
        value = (0x7F800001, 0xFFC12345, 0, 0)
        encoded = encode([value])
        self.assertEqual(len(encoded), 64)
        self.assertEqual(struct.unpack("<16I", encoded)[:4], value)

    def test_native_fault_is_not_a_result(self):
        with self.assertRaisesRegex(ValueError, "Native fault"):
            decode_results(struct.pack("<5I", 0xC0000005, 0, 0, 0, 0), 1)

    def test_truncated_runner_output_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "protocol"):
            decode_results(bytes(19), 1)

    def test_exhaustive_half_word_coverage(self):
        cases = corpus("Vec2UnpackTexCoords", exhaustive=True, random_count=0)
        self.assertEqual(len(cases), 131072)
        self.assertEqual({c[0] >> 16 for c in cases[:65536]}, set(range(65536)))
        self.assertEqual({c[0] for c in cases[65536:]}, set(range(65536)))

    def test_first_mismatch_retains_exact_input_bits(self):
        row = first_difference([(7, 0, 0, 0)], [(128, 0, 0, 0)], [(127, 0, 0, 0)])
        self.assertEqual(row["input_u32"][0], 7)
        self.assertEqual(row["first_word"], 0)

    def test_missing_capture_is_blocked(self):
        f = {"name": "nonexistent-test-function"}
        with self.assertRaisesRegex(core.Blocked, "No native capture"):
            load_capture(f)

    def test_generated_definition_uses_production_text(self):
        path = core.ROOT / "src/bgame/bg_pmove.cpp"
        selected = definition(path, "PM_Accelerate")
        self.assertIn("void __cdecl PM_Accelerate", selected)
        self.assertIn("stopspeed->current.value", selected)
        self.assertNotIn("double __cdecl PM_MoveScale", selected)

    def test_ambiguous_definition_is_blocked(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "source.cpp"
            path.write_text("int foo() { return 1; }\nint foo() { return 2; }\n")
            with self.assertRaisesRegex(core.Blocked, "found 2"):
                definition(path, "foo")

    def test_empty_file_selection_is_unverified(self):
        with patch("retool.cli.functions", return_value=[]), patch.object(verification, "report", return_value="unused"):
            output = io.StringIO()
            with redirect_stdout(output):
                self.assertEqual(main(["verify", "--file", "src/unmapped.cpp"]), 2)
            self.assertIn('"status": "unverified"', output.getvalue())

    def test_stale_runner_is_blocked(self):
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory); runner = work / "native/Release/re_runner.exe"
            runner.parent.mkdir(parents=True); runner.write_bytes(b"old")
            core.write_json(work / "native/build.json", {"inputs": "stale", "runner": core.digest(runner)})
            with patch.object(native, "WORK", work), patch.object(native, "build_fingerprint", return_value="current"):
                with self.assertRaisesRegex(core.Blocked, "stale"):
                    native.execute(core.functions("FloatPack9S")[0], [(0, 0, 0, 0)])

    def test_stale_capture_is_blocked(self):
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory); capture_id = "a" * 64
            core.write_json(work / "latest.json", {"capture_id": capture_id})
            core.write_json(work / capture_id / "manifest.json", {"schema": 1, "oracle": {"stale": True}})
            with patch.object(verification, "fixture_dir", return_value=work):
                with self.assertRaisesRegex(core.Blocked, "Stale native capture"):
                    load_capture(core.functions("FloatPack9S")[0])

    def test_corrupt_capture_is_blocked(self):
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory); capture_id = "a" * 64
            core.write_json(work / "latest.json", {"capture_id": capture_id})
            (work / capture_id).mkdir()
            (work / capture_id / "inputs.bin").write_bytes(b"corrupt")
            core.write_json(work / capture_id / "manifest.json", {"schema": 1, "oracle": {}, "inputs_sha256": "wrong"})
            with patch.object(verification, "fixture_dir", return_value=work), patch.object(verification, "oracle_identity", return_value={}):
                with self.assertRaisesRegex(core.Blocked, "integrity"):
                    load_capture(core.functions("FloatPack9S")[0])

    def test_concurrent_json_publication_is_complete(self):
        from concurrent.futures import ThreadPoolExecutor
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "result.json"
            with ThreadPoolExecutor(4) as pool:
                list(pool.map(lambda i: core.write_json(path, {"writer": i, "values": [i] * 1000}), range(12)))
            value = core.read_json(path)
            self.assertEqual(value["values"], [value["writer"]] * 1000)
            self.assertEqual(list(Path(directory).glob("*.tmp")), [])

    def test_original_runner_receives_resolved_default_binary_path(self):
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory); runner = work / "native/Release/re_runner.exe"
            runner.parent.mkdir(parents=True); runner.write_bytes(b"runner")
            core.write_json(work / "native/build.json", {"inputs": "current", "runner": core.digest(runner)})
            resolved = Path(directory) / "BlackOpsMP.exe"
            with patch.object(native, "WORK", work), patch.object(native, "build_fingerprint", return_value="current"), \
                 patch.object(native, "reference", return_value=(resolved, None)), \
                 patch.object(native, "check_mapping"), patch.object(native, "relocations", return_value=""), \
                 patch.object(native, "run", return_value=SimpleNamespace(stdout=bytes(20))) as call:
                native.execute(core.functions("FloatPack9S")[0], [(0, 0, 0, 0)], original=True)
                self.assertEqual(call.call_args.args[0][3], resolved)
