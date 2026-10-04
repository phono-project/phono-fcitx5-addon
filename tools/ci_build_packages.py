#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Build downloadable bundled-core packages using the target's native ABI."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tarfile


ROOT = Path(__file__).resolve().parents[1]
TARGETS = {
    "debian13": "DEB", "ubuntu2404": "DEB", "fedora43": "RPM",
    "opensuse-tumbleweed": "RPM", "arch": None,
}


def run(args: list[str | Path], **kwargs) -> subprocess.CompletedProcess:
    return subprocess.run([str(value) for value in args], check=True, text=True, **kwargs)


def version_tuple(value: str) -> tuple[int, ...]:
    return tuple(int(part) for part in value.split("."))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target", choices=TARGETS, required=True)
    parser.add_argument("--core-prefix", type=Path, required=True)
    parser.add_argument("--max-glibc", required=True, help="Target release limit, or native for rolling targets")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--jobs", type=int, default=4)
    args = parser.parse_args()
    prefix = args.core_prefix.resolve()
    core = prefix / "lib/libphono_core.so"
    header = prefix / "include/phono-core/phono_api.h"
    licenses = prefix / "share/licenses/phono-core"
    for required in (core, header, licenses / "LICENSE"):
        if not required.is_file():
            raise ValueError(f"Core artifact contract is missing {required}")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    build = ROOT / "build" / f"ci-{args.target}"
    package_root = build / "package-root"
    if package_root.exists():
        shutil.rmtree(package_root)
    host_glibc = run(["getconf", "GNU_LIBC_VERSION"], capture_output=True).stdout.strip().split()[-1]
    max_glibc = host_glibc if args.max_glibc == "native" else args.max_glibc
    cpp_runtime = run(["g++", "-print-file-name=libstdc++.so.6"], capture_output=True).stdout.strip()
    runtime_versions = run(["readelf", "--version-info", cpp_runtime], capture_output=True).stdout
    cpp_limits = {}
    for namespace in ("GLIBCXX", "CXXABI"):
        found = re.findall(rf"\b{namespace}_([0-9]+(?:\.[0-9]+)+)\b", runtime_versions)
        if not found:
            raise ValueError(f"No {namespace} definitions in the native C++ runtime")
        cpp_limits[namespace] = max(found, key=version_tuple)
    run(["cmake", "-S", ROOT, "-B", build, "-G", "Ninja",
         "-DCMAKE_TOOLCHAIN_FILE=", "-DCMAKE_BUILD_TYPE=Release",
         "-DCMAKE_INSTALL_PREFIX=/usr", "-DPHONO_BUNDLE_CORE=ON", "-DPHONO_CORE_ROOT=",
         f"-DPHONO_CORE_INCLUDE_DIR={header.parent}", f"-DPHONO_CORE_LIBRARY={core}",
         f"-DPHONO_CORE_LICENSE_ROOT={licenses}", "-DBUILD_TESTING=ON"])
    run(["cmake", "--build", build, "--parallel", str(args.jobs)])
    run(["ctest", "--test-dir", build, "--output-on-failure"])
    environment = os.environ.copy()
    environment["DESTDIR"] = str(package_root)
    run(["cmake", "--install", build], env=environment)
    run([sys.executable, ROOT / "tools/verify_install.py", package_root,
         "--bundled-core", "--require-shuangpin"])
    addon = next(package_root.glob("usr/**/fcitx5/libphono.so"))
    reports = []
    for binary, limits in (
        (addon, (max_glibc, cpp_limits["GLIBCXX"], cpp_limits["CXXABI"])),
        (core, ("2.28", "3.4.31", "1.3.14")),
    ):
        flags = ["--no-rpath", "--c-api-only"] if binary == core else []
        result = run([sys.executable, ROOT / "tools/check_elf.py", binary,
                      "--max-glibc", limits[0], "--max-glibcxx", limits[1],
                      "--max-cxxabi", limits[2], *flags], capture_output=True)
        reports.append(f"{binary.name}\n{result.stdout}\n")
    (output / f"{args.target}-elf-baseline.txt").write_text("\n".join(reports))
    # Exercise the installed DSO, including its private-core RUNPATH. The user
    # does not need a separately installed core package or LD_LIBRARY_PATH.
    addon_dir = addon.parent
    loader_env = os.environ.copy()
    loader_env.pop("LD_LIBRARY_PATH", None)
    loader_env["XDG_CONFIG_HOME"] = str(build / "installed-loader/config")
    loader_env["XDG_DATA_HOME"] = str(build / "installed-loader/data")
    loader_env["FCITX_DATA_DIRS"] = f"{package_root}/usr/share/fcitx5:/usr/share/fcitx5"
    loader_env["FCITX_ADDON_DIRS"] = f"{addon_dir}:/usr/lib/fcitx5:/usr/lib64/fcitx5"
    run([build / "load_tests"], env=loader_env)
    generators = ["TGZ"]
    if TARGETS[args.target]:
        generators.append(TARGETS[args.target])
    for generator in generators:
        run(["cpack", "--config", build / "CPackConfig.cmake", "-G", generator,
             "-B", build / "packages"])
    project_version = re.search(r"\bVERSION\s+([0-9]+(?:\.[0-9]+)+)",
                                (ROOT / "CMakeLists.txt").read_text()).group(1)
    if args.target == "arch":
        arch_dir = build / "arch-package"
        arch_dir.mkdir(exist_ok=True)
        shutil.copy2(build / "ci/PKGBUILD", arch_dir / "PKGBUILD")
        arch_env = os.environ.copy()
        arch_env["PHONO_PACKAGE_ROOT"] = str(package_root)
        if os.geteuid() == 0:
            # makepkg requires a non-root builder; source/core remain read-only.
            if subprocess.run(["id", "phono-builder"], capture_output=True).returncode:
                run(["useradd", "--create-home", "phono-builder"])
            run(["chown", "-R", "phono-builder:phono-builder", arch_dir])
            run(["runuser", "-u", "phono-builder", "--", "makepkg", "--cleanbuild", "--force", "--nosign"],
                cwd=arch_dir, env=arch_env)
        else:
            run(["makepkg", "--cleanbuild", "--force", "--nosign"], cwd=arch_dir, env=arch_env)
        for package in arch_dir.glob("*.pkg.tar.zst"):
            name = package.name.removesuffix(".pkg.tar.zst")
            shutil.copy2(package, output / f"{name}-{args.target}.pkg.tar.zst")
    for pattern in ("*.tar.gz", "*.deb", "*.rpm"):
        for package in (build / "packages").glob(pattern):
            suffix = ".tar.gz" if package.name.endswith(".tar.gz") else package.suffix
            name = package.name.removesuffix(suffix)
            shutil.copy2(package, output / f"{name}-{args.target}{suffix}")
    # Inspect the actual CPack archive after generation, not only DESTDIR.
    archive = next(output.glob("*.tar.gz"))
    archive_root = build / "archive-root"
    if archive_root.exists():
        shutil.rmtree(archive_root)
    archive_root.mkdir()
    with tarfile.open(archive) as tar:
        tar.extractall(archive_root, filter="data")
    run([sys.executable, ROOT / "tools/verify_install.py", archive_root,
         "--bundled-core", "--require-shuangpin"])
    # Also install the real native package, so malformed package dependencies
    # or an incorrect native file manifest cannot hide behind a passing TGZ.
    if args.target in ("debian13", "ubuntu2404"):
        native = next(output.glob("*.deb"))
        run(["apt-get", "install", "--reinstall", "-y", native])
    elif args.target == "fedora43":
        native = next(output.glob("*.rpm"))
        action = "reinstall" if subprocess.run(["rpm", "-q", "fcitx5-phono"], capture_output=True).returncode == 0 else "install"
        run(["dnf", action, "-y", "--nogpgcheck", "--setopt=install_weak_deps=False", native])
    elif args.target == "opensuse-tumbleweed":
        native = next(output.glob("*.rpm"))
        run(["zypper", "--non-interactive", "install", "--force", "--no-recommends", "--allow-unsigned-rpm", native])
    else:
        native = next(file for file in output.glob("*.pkg.tar.zst") if "-debug-" not in file.name)
        run(["pacman", "-U", "--noconfirm", native])
    installed_env = loader_env.copy()
    installed_env["XDG_CONFIG_HOME"] = str(build / "native-loader/config")
    installed_env["XDG_DATA_HOME"] = str(build / "native-loader/data")
    installed_env["FCITX_DATA_DIRS"] = "/usr/share/fcitx5"
    installed_env["FCITX_ADDON_DIRS"] = f"/{addon_dir.relative_to(package_root)}"
    run([build / "load_tests"], env=installed_env)
    core_info = json.loads((prefix / "share/phono-core/build-info.json").read_text())
    (output / f"{args.target}-core-build.json").write_text(json.dumps(core_info, indent=2) + "\n")
    metadata = {
        "target": args.target, "version": project_version,
        "architecture": "x86_64", "container_digest": os.environ.get("CONTAINER_DIGEST", "local"),
        "native_glibc": host_glibc, "addon_glibc_gate": max_glibc,
        "native_cpp_runtime": cpp_limits, "core_glibc_gate": "2.28",
        "core_revision": json.loads((ROOT / "ci/core-source.json").read_text())["revision"],
        "core_sha256": hashlib.sha256(core.read_bytes()).hexdigest(),
    }
    (output / f"{args.target}-build.json").write_text(json.dumps(metadata, indent=2) + "\n")
    checksums = []
    for file in sorted(output.iterdir()):
        if file.is_file() and not file.name.endswith("SHA256SUMS"):
            checksums.append(f"{hashlib.sha256(file.read_bytes()).hexdigest()}  {file.name}\n")
    (output / f"{args.target}-SHA256SUMS").write_text("".join(checksums))
    print(f"Downloadable binaries, ABI reports and checksums: {output}")


if __name__ == "__main__":
    main()
