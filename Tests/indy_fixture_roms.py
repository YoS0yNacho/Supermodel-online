#!/usr/bin/env python3
"""Rebuild private Indy program/data regions from the user's own ROM ZIP.

Writes binary ROM data locally. Do not distribute or commit the results.
The format is deliberately restricted to the Indy revision-A region layout.
"""
import argparse
from pathlib import Path
import xml.etree.ElementTree as ET
import zipfile


def extract(zip_path, xml_path, out):
    out = Path(out)
    out.mkdir(parents=True, exist_ok=False)
    game = ET.parse(xml_path).find(".//game[@name='indy500']")
    if game is None:
        raise ValueError('indy500 game missing in XML')
    with zipfile.ZipFile(zip_path) as archive:
        for region, size in (('program', 0x200000), ('main_data', 0x2000000), ('copro_data', 0x800000)):
            spec = game.find(f"./roms/region[@name='{region}']")
            if spec is None or int(spec.get('stride'),0) != 4 or int(spec.get('chunk_size'),0) != 2:
                raise ValueError(f'unexpected {region} layout')
            dest = bytearray(size)
            files = spec.findall('file')
            if not files:
                raise ValueError(f'no files for {region}')
            for item in files:
                name = item.get('name')
                offset = int(item.get('offset'), 0)
                raw = archive.read(name)  # zipfile checks its CRC during read
                if archive.getinfo(name).CRC != int(item.get('crc32'), 0):
                    raise ValueError(f'{name}: ROM CRC differs from XML')
                if not raw or len(raw) % 2 or offset < 0 or offset % 4 not in (0,2) or offset + len(raw)*2 - 2 > size:
                    raise ValueError(f'{name}: invalid region bounds')
                dest[offset:offset+len(raw)*2:4] = raw[0::2]
                dest[offset+1:offset+len(raw)*2:4] = raw[1::2]
            path = out / f'{region}.bin'
            path.write_bytes(dest)
            print(f'{region}: {path} ({len(dest)} bytes)')
    print('Private ROM bytes: keep locally; do not commit or redistribute.')


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('rom_zip'); p.add_argument('games_xml'); p.add_argument('new_output_directory')
    a=p.parse_args(); extract(a.rom_zip,a.games_xml,a.new_output_directory)

if __name__=='__main__': main()
