#!/usr/bin/env python3
"""ROM-free checks for the Indy local-player contact report."""
import tempfile
import unittest
from pathlib import Path
from indy_contact_report import summarize


class ContactReportTest(unittest.TestCase):
    def report(self, text, hook=False):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / 'state.log'
            p.write_text(text)
            return summarize(p, hook)

    def test_peer_flag_and_same_frame_writes(self):
        data = ('S 10 5 7 4 1 0 2 3 1\n'
                'H 11 0 1 0053c984 0 0 3 4 3 0\n'
                'H 12 0 4 0053cf30 0 0 0 0 5 0\n'
                'I 11 020ec4 0053c59c 00000000 00000000 1\n'
                'I 11 020ed0 0053c5a4 00000000 00000000 1\n')
        r = self.report(data, True)
        self.assertEqual(r['peer_candidate_flags'], 1)
        self.assertEqual(r['peer_flags_with_same_frame_physics_xy_writes'], 1)
        self.assertEqual(r['peer_cache_to_physics']['max'], 5)
        self.assertEqual(r['peer_physics_to_candidate']['max'], 4)
        self.assertEqual(r['candidate_flags'], 2)
        self.assertIsNone(self.report(data)['peer_events'][0]['same_frame_physics_xy_writes'])

    def test_read_history_shift_and_clamp(self):
        data = ('S 10 5 7 4 1 0 2 3 1\n'
                # Native current 3.0, previous 2.0; shifted to 8.0 and 7.0.
                'Q 11 0ac4e8 0 40400000 41000000\n'
                'Q 11 0ac504 0 40000000 40e00000\n'
                # Native delta 8.0 gets clamped to zero.
                'Q 11 0ac4fc 0 41000000 41200000\n'
                'Q 11 0ac510 0 00000000 41200000\n')
        r = self.report(data)
        self.assertEqual(r['experimental_read_history_pairs'], 2)
        self.assertEqual(r['experimental_history_clamps'], 1)
        self.assertEqual(r['experimental_history_mismatches'], 0)

    def test_no_unsupported_peer_classification(self):
        data = ('S 1 5 7 4 1 0 3 7 1\n'
                'H 2 0 1 0053c984 0 0 3 4 3 0\n')
        self.assertEqual(self.report(data)['peer_candidate_flags'], 0)

    def test_incomplete_and_malformed_records(self):
        self.assertTrue(self.report('S 1 5 7 4 1 0 2 3 1\nH 1 0')['truncated_tail'])
        with self.assertRaisesRegex(ValueError, 'expected 11 H fields'):
            self.report('H 1 0\n')
        with self.assertRaisesRegex(ValueError, 'non-finite'):
            self.report('H 1 0 1 0053c984 nan 0 0 0 0 0\n')


if __name__ == '__main__':
    unittest.main()
