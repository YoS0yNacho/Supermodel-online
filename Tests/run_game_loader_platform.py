#!/usr/bin/env python3
"""ROM/SDL-free GameLoader integration tests. Requires g++, gcc and zlib headers."""
import argparse
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    flags = ['-g', '-O1']
    if args.sanitize:
        flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                  '-fno-pie', '-no-pie']
    with tempfile.TemporaryDirectory(prefix='supermodel-platform-') as tmp:
        work = Path(tmp)
        objects = []
        # Compile the bundled minizip as C, as in the emulator build.
        for source in ('unzip', 'ioapi'):
            obj = work / (source + '.o')
            subprocess.run(['gcc', '-ISrc/Pkgs', *flags, '-c',
                            f'Src/Pkgs/{source}.c', '-o', str(obj)], cwd=ROOT, check=True)
            objects.append(str(obj))
        binary = work / 'game-loader-platform'
        subprocess.run([
            'g++', '-std=c++17', '-Wall', '-Wextra', '-Wno-unused-parameter',
            '-ISrc', '-ISrc/OSD/SDL', *flags,
            'Tests/GameLoaderPlatformTest.cpp', 'Src/GameLoader.cpp',
            'Src/ROMSet.cpp', 'Src/Util/NewConfig.cpp', 'Src/Util/ConfigBuilders.cpp',
            'Src/Util/Format.cpp', 'Src/Util/ByteSwap.cpp', 'Src/OSD/Logger.cpp',
            'Src/Pkgs/tinyxml2.cpp', *objects, '-lz', '-pthread', '-o', str(binary)
        ], cwd=ROOT, check=True)
        database = ROOT / 'Config/Games.xml'
        text = database.read_text(encoding='utf-8')
        canonical = '<platform>Real3D PRO-1000</platform>'
        if canonical not in text:
            raise RuntimeError('Bundled PRO-1000 platform entry not found')
        mixed = work / 'MixedGames.xml'
        mixed.write_text(text.replace(canonical, '<platform>Real3D Pro-1000</platform>'),
                         encoding='utf-8')
        subprocess.run([str(binary), str(work / 'fixture.xml'), str(database), str(mixed)],
                       cwd=ROOT, check=True)


if __name__ == '__main__':
    main()
