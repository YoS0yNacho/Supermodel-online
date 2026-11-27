#!/usr/bin/env python3
"""ROM-free checks of the exploratory counterfactual's baseline gate."""
import contextlib
import io
import subprocess
import unittest
from unittest.mock import patch
import indy_peer_counterfactual as m

COMM = 'COMM_DIAGNOSTIC captured=1 unchanged=1 events_equal=1 reads=1 (subset)\n'
BASE = (COMM + 'BOUNDARY cpu=1 sharc=1 uart=1 memory=1 fifo_buffer=1 events_including_stalls=1 timing=1 flags=1 lines=12\n'
        + m.PATH + '\nINCONCLUSIVE\n')
PEER = 'EXPLORATORY_UNVALIDATED mode=native peer_selected=1 flag=1 g0=0053c984\nINCONCLUSIVE\n'
OTHER = 'EXPLORATORY_UNVALIDATED mode=native peer_selected=0 flag=0 g0=00000000\nINCONCLUSIVE\n'
RESP = COMM + 'EXPLORATORY_UNVALIDATED_RESPONSE native_same_peer=1 physics_x_equal=0\nINCONCLUSIVE\n'


def result(text, code=1, err=''):
    return subprocess.CompletedProcess([], code, text, err)


class GateTest(unittest.TestCase):
    def invoke(self, results):
        with patch.object(m, 'run', side_effect=results) as r, contextlib.redirect_stdout(io.StringIO()) as output:
            status = m.explore('binary', ['a']*6)
        return status, output.getvalue(), r

    def test_bad_baseline_never_runs_native(self):
        for result_value in (result('INCONCLUSIVE\n'), result(BASE.replace('timing=1','timing=0')),
                             result(BASE.replace('events_equal=1','events_equal=0')), result(BASE, 0)):
            with self.subTest(value=result_value):
                code, output, r = self.invoke([result_value])
                self.assertEqual(code, 1)
                self.assertEqual(r.call_count, 1)
                self.assertIn('native not run', output)

    def test_different_peer_cannot_run_response(self):
        code, output, r = self.invoke([result(BASE), result(OTHER)])
        self.assertEqual(code, 1)
        self.assertEqual(r.call_count, 2)
        self.assertIn('no comparable native response run', output)

    def test_same_peer_still_inconclusive(self):
        code, output, r = self.invoke([result(BASE), result(PEER), result(RESP)])
        self.assertEqual(code, 1)
        self.assertEqual(r.call_count, 3)
        self.assertIn('no full-board validation', output)

    def test_legacy_comm_capture_warns_no_witness(self):
        code, output, r = self.invoke([result(BASE), result(PEER), result(RESP)])
        self.assertEqual(code, 1)
        self.assertIn('no mutation-entry witness', output)

    def test_witnessed_capture_avoids_legacy_warning(self):
        witnessed = 'COMM_WITNESS version=2 entrypoints=1 (external queues not captured)\n'
        code, output, r = self.invoke([result(witnessed+BASE), result(PEER), result(RESP)])
        self.assertEqual(code, 1)
        self.assertNotIn('no mutation-entry witness', output)

    def test_legacy_capture_warns_no_comm_snapshot(self):
        legacy = 'COMM_DIAGNOSTIC captured=0 unchanged=0 events_equal=0 reads=0 (subset)\n'
        code, output, r = self.invoke([result(BASE.replace(COMM, legacy)), result(PEER), result(RESP.replace(COMM, legacy))])
        self.assertEqual(code, 1)
        self.assertEqual(r.call_count, 3)
        self.assertIn('no comm snapshot', output)

    def test_unsupported_response_still_fails(self):
        code, output, r = self.invoke([result(BASE), result(PEER), result('INCONCLUSIVE\n')])
        self.assertEqual(code, 1)
        self.assertEqual(r.call_count, 3)
        self.assertIn('unsupported', output)


if __name__ == '__main__':
    unittest.main()
