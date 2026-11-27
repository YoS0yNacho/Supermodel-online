#!/usr/bin/env python3
"""Optional private v2 scheduler diagnostics; the output is always inconclusive.

Set INDY_TIMED_REPLAY_BINARY, INDY_TIMED_REPLAY_PREFIX (without .in),
INDY_TIMED_REPLAY_PROGRAM, INDY_TIMED_REPLAY_MAIN_DATA, and
INDY_TIMED_REPLAY_COPRO_DATA. Never publish ROMs or captured guest RAM.
"""
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

NAMES = ('INDY_TIMED_REPLAY_BINARY', 'INDY_TIMED_REPLAY_PREFIX',
         'INDY_TIMED_REPLAY_PROGRAM', 'INDY_TIMED_REPLAY_MAIN_DATA',
         'INDY_TIMED_REPLAY_COPRO_DATA')


@unittest.skipUnless(all(os.environ.get(k) for k in NAMES), 'private timed fixture not configured')
class TimedFixtureTest(unittest.TestCase):
    def run_replay(self, io=None, entry=None, output=None, extra=()):
        binary, prefix, program, data, copro = (os.environ[k] for k in NAMES)
        return subprocess.run([binary, entry or prefix+'.in', output or prefix+'.out', program, data, copro,
                               io or prefix+'.io', *extra], text=True, capture_output=True, timeout=30)

    def test_boundary_and_inconclusive_status(self):
        p = self.run_replay()
        self.assertEqual(p.returncode, 1, p.stdout+p.stderr)
        self.assertIn('BOUNDARY cpu=1 sharc=1 uart=1 memory=1 fifo_buffer=1 events_including_stalls=1 timing=1 flags=1', p.stdout)
        self.assertIn('INCONCLUSIVE', p.stdout)

    @unittest.skipUnless(os.environ.get('INDY_TIMED_RESPONSE_FIXTURE') == '1', 'peer response fixture not configured')
    def test_peer_path_baseline_and_native_remain_unvalidated(self):
        baseline = self.run_replay()
        self.assertEqual(baseline.returncode, 1)
        self.assertIn('BOUNDARY cpu=1 sharc=1 uart=1 memory=1 fifo_buffer=1 events_including_stalls=1 timing=1 flags=1', baseline.stdout)
        self.assertIn('UNVALIDATED_REPLAY_PATH selected_peer=1', baseline.stdout)
        detector = self.run_replay(extra=('--explore-detector-experimental',))
        self.assertEqual(detector.returncode, 1)
        self.assertIn('EXPLORATORY_UNVALIDATED mode=experimental peer_selected=1 flag=1', detector.stdout)
        self.assertIn('fifo_prefix_equal=1', detector.stdout)
        native = self.run_replay(extra=('--explore-response-native',))
        self.assertEqual(native.returncode, 1)
        if 'EXPLORATORY_UNVALIDATED_RESPONSE' in native.stdout:
            self.assertIn('native_same_peer=1', native.stdout)
            self.assertIn('INCONCLUSIVE', native.stdout)
        else:
            self.assertIn('INCONCLUSIVE:', native.stderr)

    @unittest.skipUnless(os.environ.get('INDY_TIMED_COMM_FIXTURE') == '1', 'comm fixture not configured')
    def test_comm_read_and_transcript_fail_closed(self):
        p = self.run_replay()
        self.assertEqual(p.returncode, 1, p.stdout+p.stderr)
        self.assertIn('COMM_DIAGNOSTIC captured=1 unchanged=1 events_equal=1', p.stdout)
        prefix = os.environ[NAMES[1]]
        with tempfile.TemporaryDirectory() as d:
            io = Path(d)/'fixture.io'
            io.write_bytes(Path(prefix+'.io').read_bytes())
            comm = io.with_suffix('.comm')
            events = Path(prefix+'.comm').read_text().splitlines()
            witnessed = bool(events and events[0].startswith('B '))
            read_at = next((i for i, event in enumerate(events) if event.startswith('R ')), None)
            if read_at is not None:
                fields = events[read_at].split()
                fields[-1] = f'{int(fields[-1], 16) ^ 1:08x}'
                events[read_at] = ' '.join(fields)
                comm.write_text('\n'.join(events)+'\n')
                result = self.run_replay(io=str(io))
                self.assertEqual(result.returncode, 1)
                self.assertIn('comm read transcript mismatch', result.stderr)
                self.assertNotIn('BOUNDARY', result.stdout)
            else:
                fake_read = 'R 000a6538 01a121c8 ffff0000 00000000'
                lines = [events[0], fake_read, events[-1]] if witnessed else [fake_read]
                comm.write_text('\n'.join(lines)+'\n')
                result = self.run_replay(io=str(io))
                self.assertEqual(result.returncode, 1)
                self.assertIn('unconsumed comm events', result.stderr)
            write = 'W 000a6538 01a121c8 ffffffff 00000000'
            lines = [events[0], write, events[-1]] if witnessed else [write]
            comm.write_text('\n'.join(lines)+'\n')
            result = self.run_replay(io=str(io))
            self.assertIn('comm writes unsupported', result.stderr)
            if witnessed:
                for lines, error in ((events[:-1], 'incomplete comm witness'),
                                     ([], 'incomplete comm witness'),
                                     ([events[0], 'X 00000000 00000001 00000000 00000000', events[-1]],
                                      'unsupported comm mutation/control entry witnessed')):
                    comm.write_text('\n'.join(lines)+'\n' if lines else '')
                    result = self.run_replay(io=str(io))
                    self.assertIn(error, result.stderr)
                    self.assertNotIn('BOUNDARY', result.stdout)
            comm.write_text('R 000a6538')
            result = self.run_replay(io=str(io))
            self.assertIn('truncated or oversized comm transcript', result.stderr)
            comm.unlink()
            result = self.run_replay(io=str(io))
            self.assertIn('unable to open file', result.stderr)

    @unittest.skipUnless(os.environ.get('INDY_TIMED_COMM_FIXTURE') == '1', 'comm fixture not configured')
    def test_comm_boundary_mutation_is_rejected(self):
        original = Path(os.environ[NAMES[1]]+'.out').read_bytes()
        with tempfile.TemporaryDirectory() as d:
            for wanted in (b'Fixture comm view', b'Model 2 comm'):
                with self.subTest(block=wanted):
                    pos = 0
                    while pos < len(original):
                        length, namesize, commentsize = struct.unpack_from('<III', original, pos)
                        if original[pos+12:pos+12+namesize].rstrip(b'\0') == wanted:
                            break
                        pos += length
                    self.assertLess(pos, len(original))
                    payload = pos+12+namesize+commentsize
                    changed = bytearray(original)
                    changed[payload+(8 if wanted==b'Fixture comm view' else 0)] ^= 1
                    output = Path(d)/'modified.out'
                    output.write_bytes(changed)
                    p = self.run_replay(output=str(output))
                    self.assertEqual(p.returncode, 1)
                    self.assertIn('comm state changed between boundaries', p.stderr)
                    self.assertNotIn('BOUNDARY', p.stdout)

    def test_corrupt_event_is_not_accepted(self):
        with tempfile.TemporaryDirectory() as d:
            io = Path(d)/'corrupt.io'
            lines = Path(os.environ[NAMES[1]]+'.io').read_text().splitlines()
            self.assertTrue(lines)
            fields = lines[0].split()
            fields[4] = f'{int(fields[4], 16) ^ 1:08x}'
            lines[0] = ' '.join(fields)
            io.write_text('\n'.join(lines)+'\n')
            if os.environ.get('INDY_TIMED_COMM_FIXTURE') == '1':
                io.with_suffix('.comm').write_bytes(Path(os.environ[NAMES[1]]+'.comm').read_bytes())
            p = self.run_replay(str(io))
            self.assertEqual(p.returncode, 1)
            self.assertIn('events_including_stalls=0', p.stdout)
            self.assertIn('INCONCLUSIVE', p.stdout)

    def test_unsupported_callback_and_timer_rejected(self):
        original = Path(os.environ[NAMES[1]]+'.in').read_bytes()
        pos = 0
        timing = None
        while pos < len(original):
            length, namesize, commentsize = struct.unpack_from('<III', original, pos)
            self.assertGreaterEqual(length, 12 + namesize + commentsize)
            name = original[pos+12:pos+12+namesize].rstrip(b'\0')
            if name == b'Fixture timing':
                timing = pos+12+namesize+commentsize
                break
            pos += length
        self.assertIsNotNone(timing)
        self.assertEqual(struct.unpack_from('<I', original, timing)[0], 2)
        with tempfile.TemporaryDirectory() as d:
            for offset, message in ((165, 'unsupported slice or callback state'),
                                    (96, 'active timers are not yet supported')):
                with self.subTest(offset=offset):
                    changed = bytearray(original)
                    changed[timing+offset] = 1
                    entry = Path(d)/'modified.in'
                    entry.write_bytes(changed)
                    p = self.run_replay(entry=str(entry))
                    self.assertEqual(p.returncode, 1)
                    self.assertIn(message, p.stderr)
                    self.assertNotIn('BOUNDARY', p.stdout)

    def test_irq_and_burst_resume_rejected(self):
        original = Path(os.environ[NAMES[1]]+'.in').read_bytes()
        pos = 0
        offset = None
        while pos < len(original):
            length, namesize, commentsize = struct.unpack_from('<III', original, pos)
            self.assertGreaterEqual(length, 12 + namesize + commentsize)
            if original[pos+12:pos+12+namesize].rstrip(b'\0') == b'Fixture i960':
                offset = pos+12+namesize+commentsize
                self.assertEqual(pos+length-offset, 499)
                break
            pos += length
        self.assertIsNotNone(offset)
        with tempfile.TemporaryDirectory() as d:
            for relative in (464, 476, 493, 495):
                with self.subTest(relative=relative):
                    changed = bytearray(original)
                    changed[offset+relative] = 1 if relative != 495 else 2
                    entry = Path(d)/'modified.in'
                    entry.write_bytes(changed)
                    p = self.run_replay(entry=str(entry))
                    self.assertEqual(p.returncode, 1)
                    self.assertIn('unsupported pending interrupt or stalled i960 resume', p.stderr)
                    self.assertNotIn('BOUNDARY', p.stdout)

    def test_truncated_blocks_cannot_read_next_block(self):
        original = Path(os.environ[NAMES[1]]+'.in').read_bytes()
        cases = ((b'Fixture i960', 'unexpected i960 block length'),
                 (b'Fixture SHARC', 'unexpected SHARC block length'),
                 (b'Fixture devices', 'invalid buffer length'),
                 (b'Fixture timing', 'requires v2 timing fixture'),
                 (b'Fixture UART', 'truncated UART block'))
        with tempfile.TemporaryDirectory() as d:
            for target, error in cases:
                with self.subTest(block=target):
                    pos = 0
                    while pos < len(original):
                        length, namesize, commentsize = struct.unpack_from('<III', original, pos)
                        self.assertGreaterEqual(length, 12 + namesize + commentsize)
                        if original[pos+12:pos+12+namesize].rstrip(b'\0') == target:
                            break
                        pos += length
                    self.assertLess(pos, len(original))
                    modified = bytearray(original[:pos+length-1] + original[pos+length:])
                    struct.pack_into('<I', modified, pos, length-1)
                    entry = Path(d)/'short-block.in'
                    entry.write_bytes(modified)
                    p = self.run_replay(entry=str(entry))
                    self.assertEqual(p.returncode, 1)
                    self.assertIn(error, p.stderr)
                    self.assertNotIn('BOUNDARY', p.stdout)

    def test_truncated_transcript_is_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            io = Path(d)/'truncated.io'
            io.write_bytes(Path(os.environ[NAMES[1]]+'.io').read_bytes()+b'R 000')
            if os.environ.get('INDY_TIMED_COMM_FIXTURE') == '1':
                io.with_suffix('.comm').write_bytes(Path(os.environ[NAMES[1]]+'.comm').read_bytes())
            p = self.run_replay(str(io))
            self.assertEqual(p.returncode, 1)
            self.assertIn('truncated or oversized transcript', p.stderr)


if __name__ == '__main__':
    unittest.main()
