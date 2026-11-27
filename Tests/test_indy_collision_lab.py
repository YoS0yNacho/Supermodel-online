#!/usr/bin/env python3
"""ROM-free checks for the Indy collision lab's phase detection."""
import os
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

# Values are only resolved on import; tests do not launch a binary.
os.environ.setdefault('INDY_TRACE_BINARY', '/tmp/not-an-emulator')
os.environ.setdefault('INDY_COLLISION_OUTPUT', '/tmp/not-a-lab-output')
from run_indy_collision_lab import in_race, state, steering_cabinet, verify_fixtures, capture_ready, wait_for_capture, udp_relay_factory


class IndyCollisionLabTest(unittest.TestCase):
    def test_udp_relay_shares_tcp_impairment(self):
        from run_indy_collision_lab import relay
        legacy = relay.Room(log=lambda _: None, latency_ms=123, jitter_ms=7)
        udp = udp_relay_factory(legacy)()
        self.assertIs(udp.latency, legacy.latency)
        self.assertEqual((udp.latency.latency_ms, udp.latency.jitter_ms), (123, 7))

    def test_race_requires_active_two_seat_roster_and_local_actor(self):
        self.assertTrue(in_race((5000, 5, 7, 4, 1, 0, 2, 3, 1)))
        self.assertTrue(in_race((5000, 5, 7, 4, 1, 1, 2, 3, 1)))
        self.assertFalse(in_race(None))
        self.assertFalse(in_race((5000, 5, 7, 4, 1, 1, 2, 1, 1)))
        self.assertFalse(in_race((5000, 5, 7, 4, 1, 2, 2, 3, 1)))
        self.assertFalse(in_race((5000, 5, 7, 4, 0, 0, 2, 3, 0)))
        self.assertFalse(in_race((5000, 5, 7, 4, 1, 0, 1, 1, 1)))

    def test_actor_target_tracks_race_mapping(self):
        actor1 = (5000, 5, 7, 4, 1, 1, 2, 3, 1)
        actor0 = (5000, 5, 7, 4, 1, 0, 2, 3, 1)
        self.assertEqual(steering_cabinet(0, 'actor', [actor1, actor0]), 1)
        self.assertEqual(steering_cabinet(0, 'cabinet', [actor1, actor0]), 0)
        with self.assertRaisesRegex(ValueError, 'ambiguous'):
            steering_cabinet(0, 'actor', [actor0, actor0])
        with self.assertRaises(ValueError):
            steering_cabinet(0, 'unknown', [actor1, actor0])

    def test_fixture_completeness(self):
        with tempfile.TemporaryDirectory() as d:
            cabs = [Path(d)/'cab0', Path(d)/'cab1']
            for cab in cabs: cab.mkdir()
            with self.assertRaisesRegex(RuntimeError, 'no collision'):
                verify_fixtures(cabs)
            (cabs[0]/'fixture-0.in').touch()
            with self.assertRaisesRegex(RuntimeError, 'incomplete'):
                verify_fixtures(cabs)
            for name in ('fixture-0.out', 'fixture-0.io', 'fixture-program.bin'):
                (cabs[0]/name).touch()
            self.assertEqual(verify_fixtures(cabs),1)

    def test_capture_ready_requires_both_completed(self):
        with tempfile.TemporaryDirectory() as d:
            cabs = [Path(d)/'cab0', Path(d)/'cab1']
            for cab in cabs:
                cab.mkdir()
                for name in ('fixture-0.in', 'fixture-0.io', 'fixture-0.out', 'fixture-program.bin'):
                    (cab/name).touch()
                (cab/'state.log').write_text('FIXTURE_BEGIN 0 100 0 00d794 5.0\n')
            self.assertFalse(capture_ready(cabs, None))
            (cabs[0]/'state.log').write_text('FIXTURE_END 0 100 00d794\n')
            self.assertFalse(capture_ready(cabs, None))
            (cabs[1]/'state.log').write_text('FIXTURE_END 0 100 00d794')
            self.assertFalse(capture_ready(cabs, None))
            (cabs[1]/'state.log').write_text('prefix FIXTURE_END 0 100 00d794\n')
            self.assertFalse(capture_ready(cabs, None))
            (cabs[1]/'state.log').write_text('FIXTURE_END 0 100 00d794\n')
            self.assertTrue(capture_ready(cabs, None))
            with patch.dict(os.environ, {'SUPERMODEL_INDY_LAB_COMM_FIXTURE': '1'}):
                self.assertFalse(capture_ready(cabs, None))
                with self.assertRaisesRegex(RuntimeError, 'incomplete'):
                    verify_fixtures(cabs)
                for cab in cabs:
                    (cab/'fixture-0.comm').touch()
                self.assertTrue(capture_ready(cabs, None))
                self.assertEqual(verify_fixtures(cabs), 2)

    def test_latest_complete_sample(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / 'state.log'
            self.assertIsNone(state(p))
            p.write_text('S 1 3 2 0 0 0 1 1 0\nV 1 0 1.0 2.0 0 0\n'
                         'S 2 5 7 4 1 0 2 3 1\nS 3 5 7')
            self.assertEqual(state(p), (2, 5, 7, 4, 1, 0, 2, 3, 1))


class CaptureWaitTest(unittest.IsolatedAsyncioTestCase):
    async def test_success(self):
        with patch('run_indy_collision_lab.capture_ready', return_value=True):
            await wait_for_capture([], None, 1)

    async def test_timeout(self):
        with patch('run_indy_collision_lab.capture_ready', return_value=False):
            with self.assertRaisesRegex(TimeoutError, 'both cabinets'):
                await wait_for_capture([], None, .01)


if __name__ == '__main__':
    unittest.main()
