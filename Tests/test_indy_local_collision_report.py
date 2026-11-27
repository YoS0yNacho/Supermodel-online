#!/usr/bin/env python3
import struct
import tempfile
from pathlib import Path
import unittest
from indy_local_collision_report import analyze


def bits(x):
    return struct.unpack('<I', struct.pack('<f', x))[0]

def record(frame, pc, value, actor=0):
    return f'K {frame} {pc:06x} {actor} 00000000 {bits(value):08x}\n'

class TestReport(unittest.TestCase):
    def run_lines(self, text):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d)/'state.log'
            p.write_text(text)
            return analyze(p)

    def test_continuity(self):
        data = record(10,0xac4e8,1)+record(10,0xac4fc,2)+record(10,0xac504,0)+record(10,0xac510,1)
        data += record(11,0xac4e8,3)+record(11,0xac4fc,4)+record(11,0xac504,1)+record(11,0xac510,2)
        result = self.run_lines(data)
        self.assertEqual(result['history_checks'], 2)
        self.assertEqual(result['frames'], 2)

    def test_latching_and_history_errors(self):
        with self.assertRaisesRegex(ValueError, 'same-frame'):
            self.run_lines(record(1,0xac4e8,1)+record(1,0xac2ac,2))
        with self.assertRaisesRegex(ValueError, 'history mismatch'):
            self.run_lines(record(1,0xac4e8,1)+record(2,0xac504,2))

    def test_invalid_and_partial(self):
        with self.assertRaisesRegex(ValueError, 'invalid K'):
            self.run_lines(record(1,0x1234,1))
        with self.assertRaisesRegex(ValueError, 'invalid replacement'):
            self.run_lines(record(1,0xac4e8,float('nan')))
        self.assertEqual(self.run_lines(record(1,0xac4e8,1)+'K 2')['records'],1)

if __name__ == '__main__':
    unittest.main()
