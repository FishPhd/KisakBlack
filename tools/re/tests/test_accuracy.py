import unittest

from retool.core import Blocked
from retool.verification import agreement, accuracy_summary


class AccuracyTests(unittest.TestCase):
    def test_whole_case_and_bitwise(self):
        result = agreement([(0, 1), (0, 2)], [(0, 1), (0x80000000, 2)])
        self.assertEqual(result["agreement_percent"], 50)
        self.assertEqual(result["mismatched_cases"], 1)

    def test_missing_results_block(self):
        for expected, actual in [([], []), ([(1,)], [])]:
            with self.assertRaises(Blocked):
                agreement(expected, actual)

    def test_weighting_and_unknown_exclusion(self):
        rows = [dict(status="pass", emulator_qualified=True, cases=100,
                     matched_cases=100, agreement_percent=100),
                dict(status="mismatch", emulator_qualified=True, cases=2,
                     matched_cases=0, agreement_percent=0),
                dict(status="blocked", emulator_qualified=True, cases=9,
                     matched_cases=9, agreement_percent=100),
                dict(status="mismatch", emulator_qualified=False),
                dict(status="unverified")]
        result = accuracy_summary(rows)
        self.assertEqual(result["measured_units"], 2)
        self.assertEqual(result["equal_unit_agreement_percent"], 50)
        self.assertAlmostEqual(result["pooled_case_agreement_percent"], 10000 / 102)
        self.assertIsNone(result["whole_game_accuracy_percent"])
        self.assertIsNone(accuracy_summary([])["equal_unit_agreement_percent"])
