#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Validate a DESTDIR package payload and its ELF runtime-search paths."""
from __future__ import annotations

import argparse
import configparser
from pathlib import Path
import re
import stat
import subprocess


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def verify_directory_permissions(root: Path) -> None:
    for directory in root.rglob("*"):
        if directory.is_dir() and not directory.is_symlink():
            mode = stat.S_IMODE(directory.stat().st_mode)
            require(mode == 0o755,
                    f"Directory permissions must be 0755: {directory.relative_to(root)} ({mode:04o})")


def dynamic(path: Path) -> str:
    return subprocess.run(["readelf", "-d", str(path)], check=True, text=True,
                          capture_output=True).stdout


def metadata(path: Path, section: str) -> configparser.SectionProxy:
    parser = configparser.ConfigParser(interpolation=None)
    parser.optionxform = str
    parser.read(path, encoding="utf-8")
    require(section in parser, f"Missing [{section}] in {path.name}")
    return parser[section]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path, help="DESTDIR, or root of an unpacked archive")
    core = parser.add_mutually_exclusive_group(required=True)
    core.add_argument("--external-core", action="store_true")
    core.add_argument("--bundled-core", action="store_true")
    parser.add_argument("--require-shuangpin", action="store_true")
    args = parser.parse_args()
    root = args.root.resolve()
    require(root.is_dir(), f"Package root not found: {root}")
    verify_directory_permissions(root)
    files = sorted(p for p in root.rglob("*") if p.is_file())
    addon = [p for p in files if p.name == "libphono.so" and p.parent.name == "fcitx5"]
    require(len(addon) == 1, "Expected exactly one Fcitx5 libphono.so")
    require(addon[0].relative_to(root).parts[0] == "usr", "Addon must be installed under /usr")
    for relative in (
        "usr/share/fcitx5/addon/phono.conf",
        "usr/share/fcitx5/inputmethod/phono.conf",
        "usr/share/licenses/phono-fcitx5-addon/LICENSE",
        "usr/share/doc/phono-fcitx5-addon/README.md",
        "usr/share/doc/phono-fcitx5-addon/docs/zh-cn/ci.md",
        "usr/share/doc/phono-fcitx5-addon/docs/en-us/ci.md",
    ):
        require((root / relative).is_file(), f"Missing required package file: {relative}")
    addon_metadata = metadata(root / "usr/share/fcitx5/addon/phono.conf", "Addon")
    require(addon_metadata.get("Library") == "libphono" and
            addon_metadata.get("Type") == "SharedLibrary",
            "Addon metadata does not point to the shared libphono module")
    methods = [("phono.conf", "Phono - Pinyin Input", "Phono - 拼音输入")]
    if args.require_shuangpin:
        methods.append(("phono-shuangpin.conf", "Phono - Shuangpin Input", "Phono - 双拼输入"))
    for filename, english, chinese in methods:
        method_path = root / "usr/share/fcitx5/inputmethod" / filename
        require(method_path.is_file(), f"Missing input method: {filename}")
        method = metadata(method_path, "InputMethod")
        require(method.get("Addon") == "phono", f"Wrong addon in {filename}")
        require(method.get("Name") == english and method.get("Name[zh_CN]") == chinese,
                f"Wrong localized input-method names in {filename}")
    for language in ("zh_CN", "zh_TW", "zh_HK"):
        catalog = root / f"usr/share/locale/{language}/LC_MESSAGES/phono-fcitx5-addon.mo"
        require(catalog.is_file() and catalog.read_bytes()[:4] in
                (b"\xde\x12\x04\x95", b"\x95\x04\x12\xde"),
                f"Missing or invalid compiled translation catalog: {language}")
    cores = [p for p in files if p.name.startswith("libphono_core.so")]
    if args.external_core:
        require(not cores, "External-core package must not copy the core DSO")
    else:
        require(len(cores) == 1 and cores[0].parent == addon[0].parent / "phono",
                "Bundled core must be in the addon's private phono directory")
        require((root / "usr/share/licenses/phono-fcitx5-addon/phono-core/LICENSE").is_file(),
                "Bundled core is missing its own license")
    for file in files:
        relative = file.relative_to(root)
        require(not any(part in {".sdk", ".pixi", "test-env", "load-env", "bins", "vocabs"}
                        for part in relative.parts), f"Development/model payload leaked: {relative}")
        require(file.suffix not in {".pte", ".a", ".pyc"}, f"Unexpected package payload: {relative}")
    addon_dynamic = dynamic(addon[0])
    require("Shared library: [libphono_core.so" in addon_dynamic,
            "Addon does not dynamically link phono-core")
    for binary in addon + cores:
        paths = re.findall(r"\((?:RUNPATH|RPATH)\).*\[(.*?)\]", dynamic(binary))
        for paths_text in paths:
            for path in paths_text.split(":"):
                require(path in {"$ORIGIN", "$ORIGIN/phono"},
                        f"Nonportable runtime search path in {binary.name}: {path}")
    if args.bundled_core:
        require("[$ORIGIN/phono]" in addon_dynamic,
                "Bundled addon needs $ORIGIN/phono RUNPATH")
    print(f"Validated {len(files)} package files; {'external' if args.external_core else 'bundled'} shared core")


if __name__ == "__main__":
    main()
