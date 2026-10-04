"""Configure the addon with Pixi dependencies and the host Fcitx/libime ABI."""
from pathlib import Path
import os
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
arguments = ["cmake", "-S", str(ROOT), "-B", str(ROOT / "build/pixi"), "-G", "Ninja",
             "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_INSTALL_PREFIX=/usr",
             "-DCMAKE_TOOLCHAIN_FILE="]
prefix = os.environ.get("CONDA_PREFIX")
if not prefix:
    raise SystemExit("Run through pixi run config")
prefixes = [prefix]
if sdk := os.environ.get("PHONO_SDK_PREFIX"):
    prefixes.insert(0, sdk)
arguments.append("-DCMAKE_PREFIX_PATH=" + ";".join(prefixes))
arguments.extend(sys.argv[1:])
subprocess.run(arguments, check=True)
