import tempfile
from pathlib import Path
import unittest

from retool.core import Blocked
from retool.event_selection import _exact_definition


class EventSelectionContracts(unittest.TestCase):
    def test_overload_selection_preserves_only_requested_definition(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "sample.cpp"
            path.write_text('int lookup(int value) { return value; }\n'
                            'int lookup(const char* value) { const char* brace = "}"; return value[0]; }\n')
            selected = _exact_definition(path, "int lookup(const char* value)")
            self.assertIn('#line 2 ', selected)
            self.assertNotIn("lookup(int value)", selected)
            self.assertIn('const char* brace = "}"; return value[0]; }', selected)

    def test_ambiguous_overload_and_missing_template_prefix_block(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "sample.cpp"
            path.write_text("int target() { return 1; }\nint target() { return 2; }\n")
            with self.assertRaisesRegex(Blocked, "found 2"):
                _exact_definition(path, "int target()")
            path.write_text("int target() { return 1; }\n")
            with self.assertRaisesRegex(Blocked, "Missing production template"):
                _exact_definition(path, "int target()", template_prefix="template<typename T>")
