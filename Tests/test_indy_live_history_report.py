#!/usr/bin/env python3
import tempfile
import unittest
from pathlib import Path
from indy_live_history_report import summarize


def record(frame, pc, value, reset=0):
    import struct
    bits = struct.unpack('<I', struct.pack('<f', value))[0]
    return f'L {frame} {pc:06x} 0 00000000 {bits:08x} {reset}\n'


class LiveHistoryReportTest(unittest.TestCase):
    def report(self, text):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / 'state.log'
            p.write_text(text)
            return summarize(p)

    def test_progression_reset_and_latching(self):
        s = record(1,0xac4e8,10,1)+record(1,0xac504,10,1)
        s += record(2,0xac4e8,11)+record(2,0xac504,10)
        s += record(2,0xac2ac,11)+record(2,0xac2c4,10)
        s += record(3,0xac4e8,50,1)+record(3,0xac504,50,1)
        r = self.report(s)
        self.assertEqual(r['sequential_history_checks'],2)
        self.assertEqual(r['reset_history_checks'],1)
        self.assertEqual(r['history_mismatches'],0)
        self.assertEqual(r['response_latch_checks'],2)
        self.assertEqual(r['response_latch_mismatches'],0)

    def test_detect_mismatch_and_truncation(self):
        s = record(1,0xac4e8,10)+record(2,0xac4e8,11)+record(2,0xac504,99)+'L 3 0ac'
        r = self.report(s)
        self.assertTrue(r['truncated_tail'])
        self.assertEqual(r['history_mismatches'],1)
        with self.assertRaisesRegex(ValueError, 'seven L fields'):
            self.report('L 1 0ac4e8\n')


if __name__ == '__main__':
    unittest.main()
