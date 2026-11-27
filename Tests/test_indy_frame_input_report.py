#!/usr/bin/env python3
import tempfile
import unittest
from pathlib import Path
from indy_frame_input_report import read_run, compare, overview, parse_script, verify_script, script_coverage


def row(frame, actor, steer=128, x=0):
    return f'B {100+frame} {10000+frame*10+actor} {frame} {actor} {steer} 255 0 {x} 0 0 0\n'

class FrameReportTest(unittest.TestCase):
    def build(self,d,swap=False):
        root=Path(d)
        for cab in (0,1):
            actor=1-cab if swap else cab
            p=root/f'cab{cab}'
            p.mkdir()
            (p/'state.log').write_text(row(0,actor)+row(1,actor,190 if actor==0 else 128,1))
        return root
    def test_actor_mapping_and_compare(self):
        with tempfile.TemporaryDirectory() as a, tempfile.TemporaryDirectory() as b:
            x=read_run(self.build(a));y=read_run(self.build(b,True))
            self.assertEqual(overview(y)['0']['cabinet'],1)
            self.assertEqual(compare(x,y)['0']['input_mismatches'],0)
            self.assertEqual(compare(x,y)['0']['local_position_difference_max'],0)
            self.assertEqual(overview(x)['actor_start_clock_skew_ms'],0.001)
    def test_script_validation_and_expected_values(self):
        script=parse_script('0,1,2,190,255,0')
        with tempfile.TemporaryDirectory() as d:
            r=read_run(self.build(d))
            self.assertEqual(verify_script(r,script), {'0':0,'1':0})
            self.assertTrue(script_coverage(r,script)[0]['complete'])
            self.assertFalse(script_coverage(r,parse_script('0,1,5,190,255,0'))[0]['complete'])
            self.assertEqual(verify_script(r,parse_script('0,1,2,191,255,0'))['0'],1)
        for text in ('', '0,0,1,0,0,0;', '0,-1,2,0,0,0', '0,0,2,0,0,0;0,1,3,0,0,0'):
            with self.assertRaises(ValueError):
                parse_script(text)

    def test_restarted_input_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            root=self.build(d)
            p=root/'cab0'/'state.log'
            p.write_text(p.read_text()+row(0,0))
            with self.assertRaisesRegex(ValueError,'restarted'):
                read_run(root)
    def test_clock_regression_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            root=self.build(d)
            p=root/'cab1'/'state.log'
            p.write_text(row(0,1)+row(1,1).replace('10011','9000'))
            with self.assertRaisesRegex(ValueError,'clock sequence'):
                read_run(root)

    def test_invalid_script_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            root=self.build(d)
            p=root/'cab1'/'state.log'
            p.write_text(p.read_text()+'BAD_INPUT_SCRIPT\n')
            with self.assertRaisesRegex(ValueError,'invalid'):
                read_run(root)

if __name__=='__main__': unittest.main()
