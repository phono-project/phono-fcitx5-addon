#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Build and verify the experimental native ARM64 Fcitx5 macOS plugin."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib.request

from ci_metadata import version
from fetch_core_sdk import native_platform
from check_macho import load_commands

ROOT = Path(__file__).resolve().parents[1]
APP = Path('/Library/Input Methods/Fcitx5.app/Contents')


def run(arguments, **kwargs):
    return subprocess.run([str(item) for item in arguments], check=True, **kwargs)


def download(name: str, asset: dict, downloads: Path) -> Path:
    archive = downloads / name
    if not archive.exists():
        temporary = archive.with_suffix('.download')
        urllib.request.urlretrieve(asset['url'], temporary)
        temporary.replace(archive)
    with archive.open('rb') as stream:
        if hashlib.file_digest(stream, 'sha256').hexdigest() != asset['sha256']:
            raise ValueError(f'Dependency checksum mismatch: {archive}')
    return archive


def extract(archive: Path, destination: Path, *, strip_root=False):
    destination.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=destination.parent) as temporary:
        stage = Path(temporary)
        previous_umask = os.umask(0o022)
        try:
            with tarfile.open(archive) as package:
                package.extractall(stage, filter='data')
        finally:
            os.umask(previous_umask)
        if strip_root:
            roots = list(stage.iterdir())
            if len(roots) != 1 or not roots[0].is_dir():
                raise ValueError(f'Expected one source root in {archive}')
            stage = roots[0]
        shutil.copytree(stage, destination, dirs_exist_ok=True)


def package_plugin(stage: Path, output: Path) -> Path:
    # A standalone archive uses the same lib/share/plugin layout as official
    # Fcitx5 macOS plugins. No SDK headers, tools or Chinese model data enter it.
    files = sorted(path.relative_to(stage).as_posix() for path in stage.rglob('*')
                   if path.is_file() and path.relative_to(stage).parts[0] != 'plugin')
    if any(path.startswith(('include/', 'bin/', 'lib/libime/', 'share/libime/')) for path in files):
        raise ValueError('SDK or native Chinese data leaked into the plugin')
    digest = hashlib.md5(usedforsecurity=False)
    data_digest = hashlib.md5(usedforsecurity=False)
    for name in files:
        payload = name.encode() + b'\0' + (stage / name).read_bytes()
        digest.update(payload)
        if name.startswith('share/'):
            data_digest.update(payload)
    descriptor = dict(files=files, version=digest.hexdigest(), data_version=data_digest.hexdigest(),
                      input_methods=['phono', 'phono-shuangpin'])
    (stage / 'plugin').mkdir(mode=0o755, exist_ok=True)
    (stage / 'plugin/phono.json').write_text(json.dumps(descriptor, indent=2) + '\n')
    output.mkdir(parents=True, exist_ok=True)
    archive = output / f'phono-fcitx5-addon-arm64-{version()}-macos.tar.bz2'
    with tarfile.open(archive, 'w:bz2') as package:
        for child in sorted(stage.iterdir()):
            package.add(child, arcname=child.name)
    return archive


def installed_loader(build: Path, stage: Path, chinese: Path):
    # Relink the copied test's search paths to the staged payload, so a build
    # SDK cannot accidentally satisfy its private libime dependencies.
    test = build / 'installed-loader/load_tests'
    test.parent.mkdir(exist_ok=True)
    shutil.copy2(build / 'load_tests', test)
    _, paths, _ = load_commands(test)
    for path in paths:
        run(['install_name_tool', '-delete_rpath', path, test])
    for path in (stage / 'lib/fcitx5/phono', APP / 'lib'):
        run(['install_name_tool', '-add_rpath', path, test])
    run(['codesign', '--force', '--sign', '-', test])
    environment = {key: value for key, value in os.environ.items() if not key.startswith('DYLD_')}
    environment.update(XDG_CONFIG_HOME=str(build / 'installed-loader/config'),
                       XDG_DATA_HOME=str(build / 'installed-loader/data'),
                       FCITX_DATA_DIRS=f'{stage}/share/fcitx5:{chinese}/share/fcitx5',
                       FCITX_ADDON_DIRS=f'{stage}/lib/fcitx5:{chinese}/lib/fcitx5')
    # Dictionary fallback first; the existing model test is reused on full runs.
    model = environment.pop('PHONO_TEST_MODEL_DIR', None)
    run([test], env=environment)
    if model:
        environment['PHONO_TEST_MODEL_DIR'] = model
        run([test], env=environment)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--core-prefix', type=Path, required=True)
    parser.add_argument('--output', type=Path, default=ROOT / 'dist/macos-arm64')
    parser.add_argument('--model-dir', type=Path)
    args = parser.parse_args()
    if native_platform() != 'macos-arm64' or os.environ.get('CI') != 'true':
        raise ValueError('Run this packaging entry point on a disposable macOS ARM64 CI runner')
    # Pixi owns Python/build tools; Xcode owns the target compiler and libc++.
    for key in ('CFLAGS', 'CXXFLAGS', 'CPPFLAGS', 'LDFLAGS', 'CPATH', 'CPLUS_INCLUDE_PATH', 'LIBRARY_PATH'):
        os.environ.pop(key, None)
    os.environ['CC'] = subprocess.check_output(['xcrun', '--find', 'clang'], text=True).strip()
    os.environ['CXX'] = subprocess.check_output(['xcrun', '--find', 'clang++'], text=True).strip()
    os.environ['MACOSX_DEPLOYMENT_TARGET'] = '14.0'
    os.environ['SDKROOT'] = subprocess.check_output(['xcrun', '--sdk', 'macosx', '--show-sdk-path'], text=True).strip()
    build = ROOT / 'build/macos-arm64'
    sdk = ROOT / '.sdk/macos'
    downloads = sdk / 'downloads'
    downloads.mkdir(parents=True, exist_ok=True)
    lock = json.loads((ROOT / 'ci/macos-sdk.json').read_text())
    archives = {name: download(name, asset, downloads) for name, asset in lock.items()}
    run(['sudo', 'mkdir', '-p', APP.parent.parent])
    run(['sudo', 'tar', 'xf', archives['fcitx'], '-C', APP.parent.parent])
    prebuilt = sdk / 'prebuilt'
    for name in ('boost', 'zstd'):
        extract(archives[name], prebuilt)
    source = sdk / 'libime-source'
    extract(archives['libime'], source, strip_root=True)
    extract(archives['kenlm'], source / 'src/libime/core/kenlm', strip_root=True)
    chinese = sdk / 'chinese'
    for name in ('chinese-data', 'chinese-modules'):
        extract(archives[name], chinese)
    # The upstream static zstd SDK is relocatable after fixing its pkg-config
    # installation prefix; headers/libraries remain normal SDK dependencies.
    pkgconfig = prebuilt / 'lib/pkgconfig/libzstd.pc'
    pkgconfig.write_text(pkgconfig.read_text().replace('prefix=/usr\n', f'prefix={prebuilt}\n'))
    os.environ['PKG_CONFIG_PATH'] = str(pkgconfig.parent)
    os.environ.pop('PKG_CONFIG_SYSROOT_DIR', None)
    native = sdk / 'native'
    prefixes = ';'.join(map(str, (APP, prebuilt, os.environ['CONDA_PREFIX'])))
    common = ['-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_OSX_ARCHITECTURES=arm64',
              '-DCMAKE_OSX_DEPLOYMENT_TARGET=14.0', f'-DCMAKE_OSX_SYSROOT={os.environ["SDKROOT"]}',
              f'-DCMAKE_CXX_COMPILER={os.environ["CXX"]}']
    ecm = Path(subprocess.check_output(['brew', '--prefix', 'extra-cmake-modules'], text=True).strip()) / 'share/ECM/cmake'
    run(['cmake', '-S', ROOT / 'ci/macos', '-B', build / 'libime', *common,
         f'-DLIBIME_SOURCE_DIR={source}', f'-DCMAKE_PREFIX_PATH={prefixes}',
         f'-DCMAKE_INSTALL_PREFIX={native}', f'-DECM_DIR={ecm}', '-DBoost_USE_STATIC_LIBS=ON'])
    run(['cmake', '--build', build / 'libime', '--parallel', '3'])
    run(['cmake', '--install', build / 'libime'])
    notices = sdk / 'notices'
    shutil.copytree(source / 'LICENSES', notices / 'libime', dirs_exist_ok=True)
    kenlm = source / 'src/libime/core/kenlm'
    for item in kenlm.glob('COPYING*'):
        (notices / 'kenlm').mkdir(exist_ok=True)
        shutil.copy2(item, notices / 'kenlm' / item.name)
    for source_file, target in [(source / 'src/libime/core/kenlm/LICENSE', 'kenlm/LICENSE'),
                                (source / 'src/libime/core/kenlm/util/double-conversion/LICENSE', 'double-conversion/LICENSE'),
                                (archives['boost-license'], 'boost/LICENSE'), (archives['zstd-license'], 'zstd/LICENSE')]:
        (notices / target).parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source_file, notices / target)
    shutil.copy2(ROOT / 'ci/macos-sdk.json', notices / 'sources.json')
    core = args.core_prefix.resolve(strict=True)
    run(['cmake', '-S', ROOT, '-B', build / 'addon', *common,
         f'-DCMAKE_PREFIX_PATH={native};{prefixes};{core}', '-DCMAKE_INSTALL_PREFIX=/usr',
         '-DPHONO_BUNDLE_CORE=ON', f'-DPHONO_SDK_PREFIX={core}',
         f'-DPHONO_MACOS_LICENSE_ROOT={notices}', f'-DPHONO_NATIVE_DATA_DIR={chinese}/share/fcitx5',
         f'-DPHONO_NATIVE_ADDON_DIR={chinese}/lib/fcitx5',
         f'-DCMAKE_BUILD_RPATH={APP}/lib;{native}/lib;{core}/lib', '-DBUILD_TESTING=ON'])
    run(['cmake', '--build', build / 'addon', '--parallel', '3'])
    environment = os.environ.copy()
    environment.pop('PHONO_TEST_MODEL_DIR', None)
    run(['ctest', '--test-dir', build / 'addon', '--output-on-failure'], env=environment)
    stage = build / 'stage'
    if stage.exists(): shutil.rmtree(stage)
    run(['cmake', '--install', build / 'addon', '--prefix', stage])
    # Build/SDK paths must not be preserved in signed distributable binaries.
    for binary in (stage / 'lib/fcitx5').rglob('*'):
        if binary.is_file() and binary.suffix in ('.so', '.dylib'):
            run(['codesign', '--force', '--sign', '-', binary])
    run([sys.executable, ROOT / 'tools/verify_install.py', stage,
         '--macos', '--bundled-core', '--require-shuangpin'])
    archive = package_plugin(stage, args.output.resolve())
    # Exercise files from the actual release archive, after extraction elsewhere.
    relocated = build / 'archive-root'
    if relocated.exists(): shutil.rmtree(relocated)
    extract(archive, relocated)
    run([sys.executable, ROOT / 'tools/verify_install.py', relocated,
         '--macos', '--bundled-core', '--require-shuangpin'])
    if args.model_dir:
        from fetch_model import FILES
        model = args.model_dir.resolve(strict=True)
        if not all((model / name).is_file() for name in FILES):
            raise ValueError('Release model is incomplete')
        os.environ['PHONO_TEST_MODEL_DIR'] = str(model)
    installed_loader(build / 'addon', relocated, chinese)
    print(f'Experimental macOS ARM64 plugin: {archive}')


if __name__ == '__main__':
    main()
