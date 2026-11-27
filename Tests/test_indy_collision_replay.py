#!/usr/bin/env python3
"""Optional integration checks using PRIVATE captured ROM and fixture files.

Set INDY_REPLAY_BINARY, INDY_REPLAY_PREFIX (without .in), INDY_REPLAY_PROGRAM,
and INDY_REPLAY_DATA. The fixtures/ROM are not part of the repository.
"""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

NAMES=('INDY_REPLAY_BINARY','INDY_REPLAY_PREFIX','INDY_REPLAY_PROGRAM','INDY_REPLAY_DATA')

@unittest.skipUnless(all(os.environ.get(n) for n in NAMES), 'private replay fixture not configured')
class ReplayIntegration(unittest.TestCase):
    def setUp(self):
        self.binary=os.environ[NAMES[0]]
        self.prefix=os.environ[NAMES[1]]
        self.prog=os.environ[NAMES[2]]
        self.data=os.environ[NAMES[3]]
        self.fixture_in=self.prefix+'.in'; self.fixture_out=self.prefix+'.out'; self.io=self.prefix+'.io'

    def run_replay(self, out=None, io=None):
        return subprocess.run([self.binary,self.fixture_in,out or self.fixture_out,self.prog,self.data,io or self.io],
                              text=True,capture_output=True,timeout=30)

    def test_reproduces_capture(self):
        p=self.run_replay()
        self.assertEqual(p.returncode,0,p.stdout+p.stderr)
        self.assertIn('work_changed_bytes=0 main_changed_bytes=0 cpu_equal=1 deterministic=1',p.stdout)
        self.assertTrue('native vs experimental' in p.stdout or 'native replay inconclusive' in p.stdout)

    def test_truncated_io_is_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            path=Path(d)/'bad.io'
            path.write_bytes(Path(self.io).read_bytes()+b'W 00000000')
            p=self.run_replay(io=str(path))
            self.assertNotEqual(p.returncode,0)
            self.assertIn('truncated or oversized transcript',p.stderr)

    def test_changed_coprocessor_write_is_rejected(self):
        lines=Path(self.io).read_text().splitlines()
        if not lines:
            self.skipTest('this fixture has no coprocessor events')
        parts=lines[0].split()
        self.assertEqual(parts[0],'W')
        parts[4]=f'{int(parts[4],16)^1:08x}'
        lines[0]=' '.join(parts)
        with tempfile.TemporaryDirectory() as d:
            path=Path(d)/'changed.io'; path.write_text('\n'.join(lines)+'\n')
            p=self.run_replay(io=str(path))
            self.assertNotEqual(p.returncode,0,p.stdout+p.stderr)
            self.assertIn('coprocessor mismatch',p.stderr)

    def test_corrupt_expected_cpu_is_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            path=Path(d)/'bad.out'; content=bytearray(Path(self.fixture_out).read_bytes())
            content[-1]^=1; path.write_bytes(content)
            p=self.run_replay(out=str(path))
            self.assertNotEqual(p.returncode,0,p.stdout+p.stderr)

if __name__=='__main__': unittest.main()
