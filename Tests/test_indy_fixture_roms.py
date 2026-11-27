#!/usr/bin/env python3
"""Synthetic ROM-free tests for the private fixture ROM builder."""
import tempfile
import unittest
import zipfile
import zlib
from pathlib import Path
from indy_fixture_roms import extract

class FixtureRomsTest(unittest.TestCase):
    def test_interleave_crc_and_exclusive_directory(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d); files={'a':b'\x11\x22\x33\x44', 'b':b'\x55\x66\x77\x88'}
            with zipfile.ZipFile(root/'test.zip','w') as z:
                for name,data in files.items(): z.writestr(name,data)
            regions=[]
            for name in ('program','main_data','copro_data'):
                entries=''.join(f'<file offset="{offset}" name="{key}" crc32="{zlib.crc32(files[key])}"/>' for key,offset in (('a',0),('b',2)))
                regions.append(f'<region name="{name}" stride="4" chunk_size="2">{entries}</region>')
            (root/'games.xml').write_text('<games><game name="indy500"><roms>'+''.join(regions)+'</roms></game></games>')
            extract(root/'test.zip',root/'games.xml',root/'out')
            for name,size in (('program',0x200000),('main_data',0x2000000),('copro_data',0x800000)):
                p=root/'out'/f'{name}.bin'
                self.assertEqual(p.stat().st_size,size)
                with p.open('rb') as f:self.assertEqual(f.read(8),bytes.fromhex('1122556633447788'))
            with self.assertRaises(FileExistsError): extract(root/'test.zip',root/'games.xml',root/'out')
            bad=(root/'games.xml').read_text().replace(str(zlib.crc32(files['a'])),'0')
            (root/'bad.xml').write_text(bad)
            with self.assertRaisesRegex(ValueError,'CRC'): extract(root/'test.zip',root/'bad.xml',root/'badout')

if __name__=='__main__': unittest.main()
