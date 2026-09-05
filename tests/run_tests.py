#!/usr/bin/env python3
"""Compile the actual component with host stubs and run packet regressions."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
HEADERS = (
    'components/sensor/sensor.h', 'components/switch/switch.h',
    'components/uart/uart.h', 'core/component.h', 'core/helpers.h',
    'core/hal.h', 'core/log.h',
)


def main():
    compiler = os.environ.get('CXX') or next(
        (found for name in ('g++', 'clang++', 'cl') if (found := shutil.which(name))), None)
    if compiler is None:
        raise SystemExit('A C++17 compiler is required. On Windows, use a Visual Studio developer shell.')
    with tempfile.TemporaryDirectory(prefix='baha-tests-') as name:
        build = Path(name)
        shutil.copyfile(ROOT / 'tests/esphome_stubs.h', build / 'esphome_stubs.h')
        for header in HEADERS:
            path = build / 'esphome' / header
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('#include "esphome_stubs.h"\n', encoding='utf-8')
        source = ROOT / 'components/baha_rs485'
        files = [str(source / 'baha_rs485.cpp'), str(ROOT / 'tests/test_heater.cpp')]
        binary = build / ('heater_tests.exe' if os.name == 'nt' else 'heater_tests')
        if Path(compiler).stem.lower() == 'cl':
            command = [compiler, '/nologo', '/std:c++17', '/EHsc', '/utf-8',
                       f'/I{build}', f'/I{source}', *files, f'/Fe:{binary}']
        else:
            command = [compiler, '-std=c++17', '-I', str(build), '-I', str(source),
                       *files, '-o', str(binary)]
        subprocess.run(command, cwd=build, check=True)
        subprocess.run([str(binary)], cwd=build, check=True)


if __name__ == '__main__':
    main()
