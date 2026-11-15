"""Regression checks for directory modes in staging trees and native packages."""
from pathlib import Path
import io
import json
import os
import subprocess
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import ci_build_packages
import verify_install
import ci_build_macos


class PackagePermissions(unittest.TestCase):
    def test_staging_rejects_world_writable_shared_directories(self):
        with tempfile.TemporaryDirectory(dir=Path.cwd()) as temporary:
            root = Path(temporary)
            addon_dir = root / "usr/lib/fcitx5"
            addon_dir.mkdir(parents=True)
            for directory in root.rglob("*"):
                directory.chmod(0o755)
            verify_install.verify_directory_permissions(root)
            for relative in ("usr", "usr/lib", "usr/lib/fcitx5"):
                directory = root / relative
                directory.chmod(0o777)
                with self.subTest(directory=relative), self.assertRaisesRegex(ValueError, "0755"):
                    verify_install.verify_directory_permissions(root)
                directory.chmod(0o755)

    def test_deb_and_arch_reject_bad_archive_directory_modes(self):
        for suffix in (".deb", ".pkg.tar.zst"):
            for mode in (0o755, 0o777):
                data = io.BytesIO()
                with tarfile.open(fileobj=data, mode="w") as archive:
                    directory = tarfile.TarInfo("usr/lib/fcitx5/")
                    directory.type = tarfile.DIRTYPE
                    directory.mode = mode
                    archive.addfile(directory)
                    regular = tarfile.TarInfo("usr/lib/fcitx5/libphono.so")
                    regular.mode = 0o644
                    archive.addfile(regular)
                result = subprocess.CompletedProcess([], 0, stdout=data.getvalue())
                with self.subTest(suffix=suffix, mode=mode), patch.object(ci_build_packages, "run", return_value=result):
                    package = Path("phono-fcitx5-addon-x86_64-0.1.0-arch" + suffix)
                    if mode == 0o755:
                        ci_build_packages.verify_package_permissions(package)
                    else:
                        with self.assertRaisesRegex(ValueError, "0755"):
                            ci_build_packages.verify_package_permissions(package)

    def test_tgz_checks_encoded_directory_modes_before_safe_extraction(self):
        with tempfile.TemporaryDirectory(dir=Path.cwd()) as temporary:
            package = Path(temporary) / 'plugin.tar.gz'
            for mode in (0o755, 0o777):
                with tarfile.open(package, 'w:gz') as archive:
                    entry = tarfile.TarInfo('usr')
                    entry.type = tarfile.DIRTYPE
                    entry.mode = mode
                    archive.addfile(entry)
                if mode == 0o755:
                    ci_build_packages.verify_package_permissions(package)
                else:
                    with self.assertRaisesRegex(ValueError, '0755'):
                        ci_build_packages.verify_package_permissions(package)

    def test_rpm_rejects_bad_directory_modes(self):
        for mode in ("40755", "40777"):
            result = subprocess.CompletedProcess([], 0, stdout=f"/usr/lib/fcitx5\t{mode}\n/usr/lib/fcitx5/libphono.so\t100644\n")
            with self.subTest(mode=mode), patch.object(ci_build_packages, "run", return_value=result):
                package = Path("phono-fcitx5-addon-x86_64-0.1.0-fedora43.rpm")
                if mode == "40755":
                    ci_build_packages.verify_package_permissions(package)
                else:
                    with self.assertRaisesRegex(ValueError, "0755"):
                        ci_build_packages.verify_package_permissions(package)

    def test_arch_recipe_normalizes_copied_directories(self):
        with tempfile.TemporaryDirectory(dir=Path.cwd()) as temporary:
            root = Path(temporary)
            source = root / "source"
            target = root / "package"
            directory = source / "usr/lib/fcitx5"
            directory.mkdir(parents=True)
            target.mkdir()
            (directory / "example").write_text("payload")
            source.chmod(0o777)
            for directory in source.rglob("*"):
                if directory.is_dir():
                    directory.chmod(0o777)
            subprocess.run(["bash", "-euc", 'source "$1"; package', "bash", str(ROOT / "ci/PKGBUILD.in")],
                           env={**os.environ, "PHONO_PACKAGE_ROOT": str(source), "pkgdir": str(target)}, check=True)
            self.assertEqual(target.stat().st_mode & 0o7777, 0o755)
            verify_install.verify_directory_permissions(target)
            self.assertEqual((target / "usr/lib/fcitx5/example").read_text(), "payload")


class MacPluginArchive(unittest.TestCase):
    def test_archive_preserves_plugin_layout_and_rejects_sdk_payload(self):
        with tempfile.TemporaryDirectory(dir=Path.cwd()) as temporary:
            root = Path(temporary)
            stage = root / 'stage'
            for name in ('lib/fcitx5/libphono.so', 'lib/fcitx5/phono/libphono_core.dylib',
                         'share/fcitx5/inputmethod/phono.conf'):
                file = stage / name
                file.parent.mkdir(parents=True, exist_ok=True)
                file.write_text('payload')
            archive = ci_build_macos.package_plugin(stage, root / 'dist')
            with tarfile.open(archive) as package:
                descriptor = json.load(package.extractfile('plugin/phono.json'))
                self.assertEqual(descriptor['input_methods'], ['phono', 'phono-shuangpin'])
                self.assertIn('lib/fcitx5/phono/libphono_core.dylib', descriptor['files'])
                self.assertFalse(any(name.startswith('usr/') for name in package.getnames()))
            model = stage / 'share/libime/sc.dict'
            model.parent.mkdir()
            model.touch()
            with self.assertRaisesRegex(ValueError, 'SDK or native Chinese data'):
                ci_build_macos.package_plugin(stage, root / 'dist')


if __name__ == "__main__":
    unittest.main()
