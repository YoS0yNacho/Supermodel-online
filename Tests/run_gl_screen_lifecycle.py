#!/usr/bin/env python3
"""Real SDL/OpenGL display lifecycle test; no ROMs or full emulator build needed.

Requires GCC/G++, SDL2 development headers, OpenGL development libraries and a
working OpenGL 4.1 display. For headless Linux, add --xvfb (requires xvfb/xauth).
The production display functions are extracted verbatim, not reimplemented.
"""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--xvfb', action='store_true')
    parser.add_argument('--source', type=Path, default=ROOT / 'Src/OSD/SDL/Main.cpp',
                        help='Main.cpp to test; defaults to the current checkout')
    args = parser.parse_args()
    source = args.source.read_text()
    # These markers deliberately fail loudly if production functions move.
    geometry_start = source.index('static Result SetGLGeometry(')
    geometry_end = source.index('// static void GLAPIENTRY DebugCallback', geometry_start)
    create_start = source.index('static Result CreateGLScreen(')
    create_end = source.index('static Result ResizeGLScreen(', create_start)
    functions = source[geometry_start:geometry_end] + source[create_start:create_end]
    sdl_cflags = shlex.split(subprocess.check_output(['sdl2-config', '--cflags'], text=True))
    sdl_libs = shlex.split(subprocess.check_output(['sdl2-config', '--libs'], text=True))
    with tempfile.TemporaryDirectory(prefix='supermodel-gl-lifecycle-') as tmp:
        work = Path(tmp)
        (work / 'DisplayFunctions.inc').write_text(functions)
        subprocess.run(['gcc', '-DGLEW_STATIC', '-ISrc/Pkgs', '-c', 'Src/Pkgs/glew.c',
                        '-o', str(work / 'glew.o')], cwd=ROOT, check=True)
        binary = work / 'gl-lifecycle'
        subprocess.run(['g++', '-std=c++17', '-O0', '-g', '-DGLEW_STATIC',
                        '-ISrc', '-I' + str(work), *sdl_cflags,
                        'Tests/GLScreenLifecycleTest.cpp', 'Src/Util/NewConfig.cpp',
                        'Src/Util/Format.cpp', str(work / 'glew.o'),
                        *sdl_libs, '-lGL', '-o', str(binary)], cwd=ROOT, check=True)
        command = [str(binary)]
        env = os.environ.copy()
        if args.xvfb:
            command = ['xvfb-run', '-a', '-s', '-screen 0 1024x768x24 -nolisten tcp', *command]
            env.update(LIBGL_ALWAYS_SOFTWARE='1', LP_NUM_THREADS='2')
        subprocess.run(command, cwd=ROOT, env=env, check=True, timeout=90)


if __name__ == '__main__':
    main()
