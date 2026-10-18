import json
from pathlib import Path
import sqlite3
import tempfile
import unittest
from contextlib import closing

from retool.binary_diff import load_inventory, summarize, read_matches, render_report
from retool.core import Blocked


def inventory(*sizes):
    return {"metadata": {"sha256": "a" * 64}, "functions": [
        {"address": 4096 + i * 256, "bytes": size, "instructions": size,
         "name": f"function_{i}", "signature": "void f()", "symbol_source": "DEFAULT"}
        for i, size in enumerate(sizes)]}


def match(a=4096, b=4096, similarity=1., confidence=1.):
    return dict(address1=a, address2=b, similarity=similarity, confidence=confidence,
                instructions=1, basicblocks=1, edges=0, match_algorithm="hash")


class BinaryDiffTests(unittest.TestCase):
    def test_unmatched_functions_remain_in_denominator(self):
        result = summarize(inventory(10, 90), inventory(10, 90), [match()], .7)
        self.assertEqual(result["overall_similarity_percent"], 10)
        self.assertEqual(result["matched_only_similarity_percent"], 100)
        self.assertEqual(result["unmatched_original_functions"], 1)
        self.assertEqual(result["unmatched_candidate_functions"], 1)

    def test_low_confidence_is_visible_but_not_credited(self):
        result = summarize(inventory(100), inventory(100), [match(confidence=.69)], .7)
        self.assertEqual(result["overall_similarity_percent"], 0)
        self.assertEqual(result["low_confidence_matches"], 1)
        self.assertEqual(result["rows"][0]["similarity_percent"], 100)
        self.assertIsNone(result["matched_only_similarity_percent"])

    def test_weighted_score_and_candidate_additions(self):
        result = summarize(inventory(10, 90), inventory(10, 90, 500),
                           [match(), match(4352, 4352, similarity=.5)], .7)
        self.assertEqual(result["overall_similarity_percent"], 55)
        self.assertEqual(result["unmatched_candidate_functions"], 1)
        self.assertAlmostEqual(sum(r["weighted_contribution_percent"] for r in result["rows"]), 55)

    def test_self_comparison_and_no_matches(self):
        a = inventory(10, 90)
        self.assertEqual(summarize(a, a, [match(), match(4352, 4352)], .7)["overall_similarity_percent"], 100)
        self.assertEqual(summarize(a, a, [], .7)["overall_similarity_percent"], 0)
        with self.assertRaises(Blocked):
            summarize(inventory(0), a, [], .7)
        with self.assertRaises(Blocked):
            summarize(a, a, [], float("nan"))

    def test_partial_and_duplicate_inventory_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp) / "inventory.jsonl"
            rows = [{"record": "metadata"}, {"record": "function", **inventory(10)["functions"][0]}]
            p.write_text("\n".join(map(json.dumps, rows)))
            with self.assertRaises(Blocked): load_inventory(p)
            rows += [rows[-1], {"record": "complete", "functions": 2}]
            p.write_text("\n".join(map(json.dumps, rows)))
            with self.assertRaises(Blocked): load_inventory(p)

    def test_database_identity_and_invalid_confidence(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp) / "diff.BinDiff"
            with closing(sqlite3.connect(p)) as db, db:
                db.executescript("CREATE TABLE metadata(file1 INT,file2 INT); INSERT INTO metadata VALUES(1,2);"
                                 "CREATE TABLE file(id INT,hash TEXT);"
                                 "CREATE TABLE functionalgorithm(id INT,name TEXT);"
                                 "CREATE TABLE function(address1 INT,address2 INT,similarity REAL,confidence REAL,algorithm INT);")
                db.executemany("INSERT INTO file VALUES(?,?)", [(1, "a"*64), (2, "b"*64)])
                db.execute("INSERT INTO function VALUES(4096,4096,1,1,1)")
            a, b = inventory(1), inventory(1)
            b["metadata"]["sha256"] = "b" * 64
            self.assertEqual(len(read_matches(p, a, b)[0]), 1)
            with self.assertRaises(Blocked): read_matches(p, b, a)
            with closing(sqlite3.connect(p)) as db, db:
                db.execute("UPDATE function SET confidence=2")
            with self.assertRaises(Blocked): read_matches(p, a, b)

    def test_report_escapes_executable_controlled_names(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp) / "report.html"
            render_report({"name": "</script><script>alert(1)</script>"}, p)
            text = p.read_text(encoding="utf-8")
            self.assertNotIn("</script><script>alert", text)
            self.assertIn("\\u003c/script", text)


if __name__ == "__main__":
    unittest.main()
