#!/usr/bin/env python3
"""ROM-free regression tests for indy_collision_trace.py."""
import tempfile
import unittest
from pathlib import Path
from indy_collision_trace import analyze, BASE, STRIDE


class CollisionTraceTest(unittest.TestCase):
    def run_trace(self, text, **kwargs):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'trace.txt'
            path.write_text(text)
            return analyze(path, **kwargs)

    def test_trigger_and_nearest_are_separate_observations(self):
        addr = BASE + 3 * STRIDE + 0x104
        report = self.run_trace(
            'P 10 0 10 10\nP 10 3 13 10\nP 10 6 14 10\n'
            f'W 10 0aa71c {addr:06x} 0000001e ffffffff\n'
            'W 10 0aa71c 53c8a4 0000001e 000000ff\n', local_actor=0)
        self.assertEqual(report['trigger_count'], 1)
        self.assertEqual(report['triggers'][0]['actor'], 3)
        self.assertEqual(report['triggers'][0]['nearest_actor'], 6)
        self.assertEqual(report['triggers'][0]['distance'], 1)
        self.assertEqual(report['nearest_local_sample']['actor'], 3)
        self.assertEqual(report['min_actors_tracked'], 3)

    def test_startup_and_missing_samples(self):
        addr = BASE + 0x104
        r = self.run_trace('P 1 0 0 0\nP 1 1 0 0\n'
                           f'W 1 0aa71c {addr:06x} 0000001e ffffffff\n'
                           'P 2 0 10 10\n', local_actor=0)
        self.assertEqual(r['valid_position_frames'], 1)
        self.assertIsNone(r['triggers'][0]['nearest_actor'])
        self.assertIsNone(r['nearest_local_sample'])
        self.assertEqual(r['min_actors_tracked'], 1)
        self.assertEqual(self.run_trace('P 1 0 1 2\nP 2 0 3 4\n', first_frame=2)['observed_frames'], 1)

    def test_truncated_tail_is_reported(self):
        r = self.run_trace('P 2 0 1 2\nW 2 0aa71c 53c8')
        self.assertTrue(r['truncated_tail'])
        self.assertEqual(r['trigger_count'], 0)
        with self.assertRaisesRegex(ValueError, 'expected six W fields'):
            self.run_trace('P 2 0 1 2\nW 2 0aa71c 53c8\n')

    def test_decreasing_frames_rejected(self):
        with self.assertRaisesRegex(ValueError, 'frame numbers decrease'):
            self.run_trace('P 2 0 1 2\nP 1 0 1 2\n')


if __name__ == '__main__':
    unittest.main()
