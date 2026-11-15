#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Download and verify the pinned core Release SDK for a native platform."""
from __future__ import annotations

import argparse
import hashlib
import json
import platform
from pathlib import Path
import shutil
import tarfile
import tempfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]


def native_arch() -> str:
    machine = platform.machine().lower()
    if machine in ('aarch64', 'arm64'):
        return 'aarch64'
    if machine in ('x86_64', 'amd64'):
        return 'x86_64'
    raise ValueError(f'Unsupported architecture: {machine}')


def native_platform() -> str:
    system = {'Linux': 'linux', 'Darwin': 'macos'}.get(platform.system())
    if not system:
        raise ValueError(f'Unsupported system: {platform.system()}')
    return f"{system}-{'arm64' if native_arch() == 'aarch64' else 'x86_64'}"


def verify(prefix: Path, target: str, lock: dict) -> None:
    if (prefix / 'share/phono-core/VERSION').read_text().strip() != lock['version']:
        raise ValueError('Installed core SDK version differs from dependency version')
    library = prefix / ('lib/libphono_core.dylib' if target.startswith('macos-') else 'lib/libphono_core.so')
    with library.open('rb') as stream:
        header = stream.read(32)
    if target.startswith('linux-'):
        expected = 183 if target.endswith('arm64') else 62
        if header[:6] != b'\x7fELF\x02\x01' or int.from_bytes(header[18:20], 'little') != expected:
            raise ValueError(f'Core library architecture differs from {target}')
    elif header[:8] != b'\xcf\xfa\xed\xfe\x0c\x00\x00\x01':
        raise ValueError(f'Core library architecture differs from {target}')
    info = json.loads((prefix / 'share/phono-core/build-info.json').read_text())
    if info['source_commit'] != lock['source_commit'] or info['dtype_selective_build']:
        raise ValueError('Core build provenance differs from pinned commit or operator policy')
    with library.open('rb') as stream:
        checksum = hashlib.file_digest(stream, 'sha256').hexdigest()
    if info['sha256'] != checksum:
        raise ValueError('Core library checksum differs from build provenance')
    for required in ('include/phono-core/phono_api.h', 'lib/cmake/phono-core/phono-coreConfig.cmake',
                     'share/licenses/phono-core/LICENSE'):
        if not (prefix / required).is_file():
            raise ValueError(f'Incomplete core SDK: {required}')


def fetch(prefix: Path, downloads: Path, target: str | None = None, *, lock: dict | None = None) -> None:
    lock = lock or json.loads((ROOT / 'ci/core-sdk.json').read_text())
    target = target or native_platform()
    asset = lock['assets'][target]
    base = f"https://github.com/{lock['repository']}/releases/download/v{lock['version']}"
    downloads.mkdir(parents=True, exist_ok=True)
    archive = downloads / asset['name']
    if not archive.is_file():
        temporary = archive.with_suffix('.download')
        urllib.request.urlretrieve(f"{base}/{asset['name']}", temporary)
        temporary.replace(archive)
    with archive.open('rb') as stream:
        if hashlib.file_digest(stream, 'sha256').hexdigest() != asset['sha256']:
            raise ValueError(f'SDK checksum mismatch: {archive}; remove the cached archive and retry')
    # Validate before touching an existing SDK. Re-extraction also repairs stale
    # prefixes after an upstream release has been rebuilt under the same tag.
    with tempfile.TemporaryDirectory(dir=downloads) as temporary:
        stage = Path(temporary)
        with tarfile.open(archive) as package:
            package.extractall(stage, filter='data')
        verify(stage, target, lock)
        shutil.copytree(stage, prefix, dirs_exist_ok=True)
    print(f"Installed core {lock['version']} ({target}): {archive.name}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prefix', type=Path, default=ROOT / '.sdk/phono-core')
    parser.add_argument('--downloads', type=Path, default=ROOT / '.sdk/downloads')
    parser.add_argument('--platform', choices=('linux-x86_64', 'linux-arm64', 'macos-arm64'))
    args = parser.parse_args()
    fetch(args.prefix.resolve(), args.downloads.resolve(), args.platform)


if __name__ == '__main__':
    main()
