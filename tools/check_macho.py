"""Validate the macOS plugin's architecture, imports and private loader paths."""
from __future__ import annotations

from pathlib import Path
import re
import subprocess

NATIVE = {'libFcitx5Core.7.dylib', 'libFcitx5Config.6.dylib', 'libFcitx5Utils.2.dylib'}
PRIVATE = {'libphono_core.dylib', 'libphono_imecore.0.dylib', 'libphono_imepinyin.0.dylib'}


def load_commands(path: Path) -> tuple[list[str], list[str], list[str]]:
    output = subprocess.check_output(['otool', '-l', path], text=True)
    imports, rpaths, identities = [], [], []
    for command in re.split(r'Load command \d+\n', output)[1:]:
        kind = re.search(r'^\s*cmd (\S+)', command, re.M).group(1)
        if kind == 'LC_RPATH':
            rpaths.append(re.search(r'\bpath (.*?) \(offset', command).group(1))
        elif kind in {'LC_LOAD_DYLIB', 'LC_LOAD_WEAK_DYLIB', 'LC_REEXPORT_DYLIB',
                      'LC_LOAD_UPWARD_DYLIB', 'LC_LAZY_LOAD_DYLIB', 'LC_ID_DYLIB'}:
            (identities if kind == 'LC_ID_DYLIB' else imports).append(
                re.search(r'\bname (.*?) \(offset', command).group(1))
    return imports, rpaths, identities


def verify(addon: Path) -> None:
    private = addon.parent / 'phono'
    libraries = [addon, *(private / name for name in sorted(PRIVATE))]
    for library in libraries:
        architectures = subprocess.check_output(['lipo', '-archs', library], text=True).strip()
        if architectures != 'arm64':
            raise ValueError(f'Expected ARM64 Mach-O: {library}: {architectures}')
        imports, rpaths, identities = load_commands(library)
        expected_identity = [] if library == addon else [f'@rpath/{library.name}']
        if identities != expected_identity:
            raise ValueError(f'Unexpected install name in {library.name}: {identities}')
        expected = {'@loader_path/phono'} if library == addon else (
            set() if library.name == 'libphono_core.dylib' else {'@loader_path'})
        if set(rpaths) != expected:
            raise ValueError(f'Unexpected loader paths in {library.name}: {rpaths}')
        for dependency in imports:
            if dependency.startswith(('/usr/lib/', '/System/Library/Frameworks/')):
                continue
            if dependency.startswith('@rpath/') and dependency.removeprefix('@rpath/') in NATIVE | PRIVATE:
                continue
            raise ValueError(f'Nonportable Mach-O dependency in {library.name}: {dependency}')
        if library == addon and '@rpath/libphono_core.dylib' not in imports:
            raise ValueError('Addon must dynamically link the core C ABI')
        if library.name == 'libphono_core.dylib' and '/usr/lib/libc++.1.dylib' not in imports:
            raise ValueError('Core must use system libc++')
        print(f'Validated {library.name}: ARM64; imports={imports}; RPATH={rpaths}')
