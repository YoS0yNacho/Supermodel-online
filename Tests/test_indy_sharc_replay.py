#!/usr/bin/env python3
"""Optional private-fixture checks for the explicitly inconclusive SHARC lab.

Set INDY_SHARC_REPLAY_BINARY, INDY_SHARC_REPLAY_PREFIX (without .in),
INDY_SHARC_REPLAY_PROGRAM, INDY_SHARC_REPLAY_MAIN_DATA and
INDY_SHARC_REPLAY_COPRO_DATA. ROM/fixture contents remain private.
"""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

NAMES=('INDY_SHARC_REPLAY_BINARY','INDY_SHARC_REPLAY_PREFIX',
       'INDY_SHARC_REPLAY_PROGRAM','INDY_SHARC_REPLAY_MAIN_DATA','INDY_SHARC_REPLAY_COPRO_DATA')

@unittest.skipUnless(all(os.environ.get(k) for k in NAMES),'private SHARC fixture not configured')
class SharcFixtureTest(unittest.TestCase):
    def command(self, io=None, extra=()):
        binary,prefix,program,data,copro=(os.environ[k] for k in NAMES)
        return [binary,prefix+'.in',prefix+'.out',program,data,copro,io or prefix+'.io',*extra]

    def run_replay(self, io=None, extra=()):
        return subprocess.run(self.command(io,extra),text=True,capture_output=True,timeout=30)

    def test_report_remains_inconclusive(self):
        p=self.run_replay()
        self.assertEqual(p.returncode,1,p.stdout+p.stderr)
        self.assertIn('experimental_cpu_equal=1 main_changed_bytes=0 work_changed_bytes=0 nonstall_fifo_events_equal=1',p.stdout)
        self.assertIn('DIAGNOSTIC_TAIL aligned=1',p.stdout)
        self.assertIn('INCONCLUSIVE: asynchronous execution is not a verified Model 2 scheduler',p.stdout)
        self.assertNotIn('EXPLORATORY_UNVALIDATED',p.stdout)

    def test_explicit_exploratory_flag_does_not_claim_validation(self):
        p=self.run_replay(extra=('--explore-native',))
        self.assertEqual(p.returncode,1,p.stdout+p.stderr)
        self.assertIn('EXPLORATORY_UNVALIDATED',p.stdout)
        self.assertIn('INCONCLUSIVE',p.stdout)

    def test_corrupted_transcript_is_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'truncated.io'
            p.write_bytes(Path(os.environ[NAMES[1]]+'.io').read_bytes()+b'R 000000')
            r=self.run_replay(io=str(p))
            self.assertEqual(r.returncode,1)
            self.assertIn('truncated or oversized transcript',r.stderr)

if __name__=='__main__': unittest.main()
