import struct
import unittest
from types import SimpleNamespace

from retool.codegen import Coff, compare_bytes, link_function
from retool.core import Blocked


def object_bytes(constant=b"\0\0\0\x3f", relocation=6):
    code = b"\xf3\x0f\x10\x05\0\0\0\0\xc3"
    name = b"?ExpandBoundsToWidth@@YAXPAM0@Z\0"
    strings = struct.pack("<I", 4 + len(name)) + name
    raw = 100
    reloc = raw + len(code) + len(constant)
    symptr = reloc + 10
    header = struct.pack("<HHIIIHH", 0x14c, 2, 0, symptr, 2, 0, 0)
    text = struct.pack("<8sIIIIIIHHI", b".text$mn", 0, 0, len(code), raw, reloc, 0, 1, 0, 0x60001020)
    data = struct.pack("<8sIIIIIIHHI", b".rdata", 0, 0, len(constant), raw + len(code), 0, 0, 0, 0, 0x40000040)
    symbol = struct.pack("<IIIhHBB", 0, 4, 0, 1, 0x20, 2, 0)
    literal = struct.pack("<8sIhHBB", b"literal", 0, 2, 0, 3, 0)
    return header + text + data + code + constant + struct.pack("<IIH", 4, 1, relocation) + symbol + literal + strings


class CodegenTests(unittest.TestCase):
    def test_bytes_and_length_are_literal(self):
        for want, got in [(b"\x90\xc3", b"\x90\xc3\x90"), (b"\x8b\xc1", b"\x8b\xc2"),
                          (b"\xa1\x01\0\0\0", b"\xa1\x02\0\0\0")]:
            self.assertEqual(compare_bytes(want, got)["status"], "mismatch")
        self.assertEqual(compare_bytes(b"\xc3", b"\xc3")["status"], "pass")
        with self.assertRaises(Blocked):
            compare_bytes(b"", b"")

    def test_standard_constant_relocation_preserves_instruction_bytes(self):
        pe = SimpleNamespace(OPTIONAL_HEADER=SimpleNamespace(ImageBase=0x400000),
                             get_data=lambda rva, size: b"\0\0\0\x3f"[:size])
        f = dict(entry_rva=0x1000, constants=[dict(rva=0x2000, size=4)])
        code, relocations = link_function(Coff(object_bytes()), "ExpandBoundsToWidth", f, pe)
        self.assertEqual(code, b"\xf3\x0f\x10\x05\0\x20\x40\0\xc3")
        self.assertEqual(relocations[0]["original_rva"], 0x2000)

    def test_ambiguous_constant_and_external_relocation_block(self):
        pe = SimpleNamespace(OPTIONAL_HEADER=SimpleNamespace(ImageBase=0x400000),
                             get_data=lambda rva, size: b"\0\0\0\x3f"[:size])
        f = dict(entry_rva=0x1000, constants=[dict(rva=0x2000, size=4), dict(rva=0x3000, size=4)])
        with self.assertRaisesRegex(Blocked, "unique"):
            link_function(Coff(object_bytes()), "ExpandBoundsToWidth", f, pe)
        with self.assertRaisesRegex(Blocked, "Unsupported COFF relocation"):
            link_function(Coff(object_bytes(relocation=0x14)), "ExpandBoundsToWidth", f, pe)

    def test_truncated_objects_block(self):
        obj = object_bytes()
        for size in (0, 19, 50, len(obj) - 1):
            with self.assertRaises(Blocked):
                Coff(obj[:size])
