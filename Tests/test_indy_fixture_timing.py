#!/usr/bin/env python3
"""Synthetic, ROM-free validation of timing fixture parsing."""
import struct
import unittest
from indy_fixture_timing import blocks, inspect


def block(name, content):
    n = name.encode() + b'\0'
    comment = b'v1\0'
    return struct.pack('<III', 12 + len(n) + len(comment) + len(content), len(n), len(comment)) + n + comment + content


def make(version=2, *, elapsed=3, budget=20, delayed=0, uart=True):
    now = 1000 + 32 * elapsed
    dev = struct.pack('<Iqqq', 1, now, 900, 32800)
    timing = (struct.pack('<II', version, 56) + bytes(56) + struct.pack('<4I', *([0] * 4))
              + struct.pack('<4I', *([0] * 4)) + bytes(4) + struct.pack('<4q', *([0] * 4))
              + struct.pack('<IIiii', 0, 1, 20, 800, elapsed))
    if version == 2:
        timing += struct.pack('<qiBB', 1000, budget, 1, delayed)
    return block('Fixture devices', dev) + block('Fixture timing', timing) + (block('Fixture UART', b'\x01') if uart else b'')


class TimingTest(unittest.TestCase):
    def test_versions(self):
        a = inspect(make(1, uart=False))
        self.assertEqual(a['version'], 1)
        self.assertFalse(a['uart_saved'])
        b = inspect(make())
        self.assertEqual(b['slice_remaining'], 17)
        self.assertFalse(b['delayed_callback'])
        self.assertTrue(inspect(make(delayed=1))['delayed_callback'])

    def test_invalid(self):
        for b in (make()[:-1], make(2, uart=False), make(budget=3), make(3),
                  make() + block('Fixture timing', b'x'), b'\0' * 12):
            with self.subTest(length=len(b)):
                with self.assertRaises(ValueError):
                    inspect(b)

    def test_out_of_bounds(self):
        with self.assertRaises(ValueError):
            blocks(struct.pack('<III', 100, 1, 1) + b'\0\0')


if __name__ == '__main__':
    unittest.main()
