#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Build a private, dynamically linked Phono C ABI with Pixi's glibc 2.28 sysroot."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
from pathlib import Path
import shutil
import shlex
import subprocess
import sys
import tarfile
import urllib.request

from check_elf import inspect

ROOT = Path(__file__).resolve().parents[1]


def run(args, **kwargs):
    return subprocess.run([str(arg) for arg in args], check=True, text=True, **kwargs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--prefix", type=Path, required=True)
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument("--executorch-source", type=Path,
                        help="Reuse a checkout only if its commit and submodules match the lock")
    parser.add_argument("--uni-algo-archive", type=Path, help="Offline copy of the locked archive")
    parser.add_argument("--jobs", type=int, default=4)
    args = parser.parse_args()
    source = args.source.resolve(strict=True)
    prefix = args.prefix.resolve()
    build = (args.build_dir or source.parent / "core-build").resolve()
    build.mkdir(parents=True, exist_ok=True)
    env_prefix = Path(os.environ.get("CONDA_PREFIX", sys.prefix)).resolve()
    sysroot = Path(os.environ.get("CONDA_BUILD_SYSROOT", ""))
    if not sysroot.is_absolute() or not sysroot.is_relative_to(env_prefix):
        raise ValueError("Run this tool through the locked ci/pixi.toml environment")
    libc = next(sysroot.glob("lib64/libc-*.so"), None)
    if not libc or "2.28" not in libc.name:
        raise ValueError(f"Expected Pixi's glibc 2.28 sysroot, found {libc}")
    compiler = Path(os.environ["CXX"])
    compiler_version = run([compiler, "-dumpfullversion"], capture_output=True).stdout.strip()
    if compiler_version != "13.3.0":
        raise ValueError(f"Expected locked GCC 13.3.0, found {compiler_version}")
    dependencies = json.loads((ROOT / "ci/core-dependencies.json").read_text())
    lock = dependencies["executorch"]
    executorch = (args.executorch_source or build / "dependencies/executorch").resolve()
    if not executorch.exists():
        executorch.parent.mkdir(parents=True, exist_ok=True)
        run(["git", "clone", "--filter=blob:none", "--no-checkout", lock["url"], executorch])
        run(["git", "-C", executorch, "checkout", "--detach", lock["commit"]])
        run(["git", "-C", executorch, "submodule", "update", "--init", "--recursive",
             "--", *lock["submodules"]])
    revision = run(["git", "-C", executorch, "rev-parse", "HEAD"], capture_output=True).stdout.strip()
    if revision != lock["commit"]:
        raise ValueError("ExecuTorch revision differs from ci/core-dependencies.json")
    status = run(["git", "-C", executorch, "submodule", "status", "--recursive",
                  "--", *lock["submodules"]], capture_output=True).stdout
    if any(line and line[0] != " " for line in status.splitlines()):
        raise ValueError("ExecuTorch submodules are missing or differ from the pinned gitlinks")
    # Code generation imports executorch.codegen and torchgen, without building
    # or installing the Python ExecuTorch extension or linking a libtorch DSO.
    environment = os.environ.copy()
    environment["PYTHONPATH"] = str(executorch.parent)
    # Conda activation adds a runtime search path to LDFLAGS. CMake's
    # CMAKE_SKIP_RPATH only controls paths CMake itself adds, so remove the
    # activation-provided RPATH before configuring any target.
    environment["LDFLAGS"] = " ".join(shlex.quote(flag) for flag in
        shlex.split(environment.get("LDFLAGS", "")) if not flag.startswith("-Wl,-rpath,"))
    # The conda-forge GCC specs also inject an absolute prefix RPATH even
    # without LDFLAGS. Override that one specs fragment for release links;
    # retain the compiler's sysroot, ABI and other toolchain specifications.
    specs = run([compiler, "-dumpspecs"], capture_output=True).stdout
    specs = re.sub(r"%\{!static:-rpath [^}\n]+\}", "", specs)
    release_specs = build / "release.specs"
    release_specs.write_text(specs)
    environment["LDFLAGS"] += f" -specs={release_specs}"
    for name in ("VCPKG_ROOT", "CMAKE_TOOLCHAIN_FILE"):
        environment.pop(name, None)
    uni_lock = dependencies["uni_algo"]
    archive = (args.uni_algo_archive or build / "dependencies/uni-algo.tar.gz").resolve()
    if not archive.exists():
        archive.parent.mkdir(parents=True, exist_ok=True)
        with urllib.request.urlopen(uni_lock["url"], timeout=120) as response:
            archive.write_bytes(response.read())
    if hashlib.sha512(archive.read_bytes()).hexdigest() != uni_lock["sha512"]:
        raise ValueError("uni-algo archive checksum mismatch")
    uni_source = build / "dependencies" / f"uni-algo-{uni_lock['version']}"
    if not uni_source.exists():
        with tarfile.open(archive) as tar:
            tar.extractall(uni_source.parent, filter="data")
    uni_prefix = build / "dependencies/install"
    common = ["-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_TOOLCHAIN_FILE=",
              f"-DCMAKE_C_COMPILER={os.environ['CC']}", f"-DCMAKE_CXX_COMPILER={compiler}",
              f"-DCMAKE_SYSROOT={sysroot}", "-DCMAKE_POSITION_INDEPENDENT_CODE=ON",
              "-DCMAKE_SKIP_RPATH=ON", f"-DCMAKE_EXE_LINKER_FLAGS={environment['LDFLAGS']}"]
    run(["cmake", "-S", uni_source, "-B", build / "uni-algo", *common,
         f"-DCMAKE_INSTALL_PREFIX={uni_prefix}", "-DUNI_ALGO_BUILD_TESTS=OFF"], env=environment)
    run(["cmake", "--build", build / "uni-algo", "--parallel", args.jobs], env=environment)
    run(["cmake", "--install", build / "uni-algo"], env=environment)
    version_script = build / "phono.exports"
    version_script.write_text("{ global: phono_*; local: *; };\n")
    run(["cmake", "-S", source, "-B", build / "native", *common,
         f"-DCMAKE_PREFIX_PATH={uni_prefix};{env_prefix}",
         f"-DPython3_EXECUTABLE={sys.executable}", f"-DEXECUTORCH_SOURCE_DIR={executorch}",
         f"-DPHONO_RUNTIME_OUTPUT_DIR={build / 'runtime'}",
         "-DPHONO_DTYPE_SELECTIVE_BUILD=OFF", "-DBUILD_TESTING=ON",
         f"-DCMAKE_SHARED_LINKER_FLAGS={environment['LDFLAGS']} -Wl,--version-script={version_script} -Wl,--exclude-libs,ALL"],
        env=environment)
    run(["cmake", "--build", build / "native", "--parallel", args.jobs,
         "--target", "phono_core_shared", "phono_capi_tests"], env=environment)
    # The build DSO intentionally has no RPATH. Only this isolated smoke test
    # receives its build directory; nothing persists to the package or Fcitx.
    test_env = {**environment, "LD_LIBRARY_PATH": str(build / "runtime")}
    run(["ctest", "--test-dir", build / "native", "-R", "^phono_capi_tests$",
         "--output-on-failure"], env=test_env)
    library = build / "runtime/libphono_core.so"
    run([os.environ.get("STRIP", "strip"), "--strip-unneeded", library])
    report = inspect(library, max_glibc="2.28", max_glibcxx="3.4.31", max_cxxabi="1.3.14",
                     no_rpath=True, c_api_only=True)
    (prefix / "lib").mkdir(parents=True, exist_ok=True)
    (prefix / "include/phono-core").mkdir(parents=True, exist_ok=True)
    shutil.copy2(library, prefix / "lib/libphono_core.so")
    shutil.copy2(source / "interface/phono_api.h", prefix / "include/phono-core/phono_api.h")
    notices = {"LICENSE": source / "LICENSE"}
    for component in ["", *lock["submodules"]]:
        directory = executorch / component
        for filename in ("LICENSE", "LICENSE.MIT", "LICENSE.txt", "COPYING", "COPYING.txt"):
            notice = directory / filename
            if notice.is_file():
                notices[f"third_party/executorch/{component}/{filename}"] = notice
    for filename in ("LICENSE", "LICENSE.md", "LICENSE.txt"):
        if (uni_source / filename).is_file():
            notices[f"third_party/uni-algo/{filename}"] = uni_source / filename
    for relative, notice in notices.items():
        target = prefix / "share/licenses/phono-core" / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(notice, target)
    metadata = {"schema_version": 1, "dependencies": dependencies,
                "source": json.loads((source / "ci-source-info.json").read_text()),
                "compiler": compiler_version, "sysroot_glibc": "2.28",
                "dtype_selective_build": False, "elf": report,
                "sha256": hashlib.sha256(library.read_bytes()).hexdigest()}
    info = prefix / "share/phono-core/build-info.json"
    info.parent.mkdir(parents=True, exist_ok=True)
    info.write_text(json.dumps(metadata, indent=2) + "\n")
    print(f"Verified shared C ABI core: {prefix}")


if __name__ == "__main__":
    main()
