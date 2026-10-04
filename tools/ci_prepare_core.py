#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Fetch an immutable core source revision and apply its reviewed runtime patch."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--local-repository", type=Path,
                        help="Use a local Git object store for offline CI-script verification")
    args = parser.parse_args()
    metadata = json.loads((ROOT / "ci/core-source.json").read_text())
    revision = metadata["revision"]
    if not re.fullmatch(r"[0-9a-f]{40}", revision):
        raise ValueError("Core source must be pinned to a full commit SHA")
    patch = (ROOT / "ci" / metadata["patch"]).resolve()
    if patch.parent != (ROOT / "ci").resolve():
        raise ValueError("Core patch must be checked into ci/")
    if hashlib.sha256(patch.read_bytes()).hexdigest() != metadata["patch_sha256"]:
        raise ValueError("Core patch checksum does not match its source lock")
    output = args.output.resolve()
    if output.exists():
        raise ValueError(f"Use a fresh core source output directory: {output}")
    repository = str(args.local_repository.resolve()) if args.local_repository else metadata["repository"]
    subprocess.run(["git", "clone", "--no-checkout", "--filter=blob:none", repository, str(output)], check=True)
    subprocess.run(["git", "-C", str(output), "checkout", "--detach", revision], check=True)
    actual = subprocess.check_output(["git", "-C", str(output), "rev-parse", "HEAD"], text=True).strip()
    if actual != revision:
        raise ValueError("Core checkout does not match the source lock")
    subprocess.run(["git", "-C", str(output), "apply", "--check", str(patch)], check=True)
    subprocess.run(["git", "-C", str(output), "apply", str(patch)], check=True)
    (output / "ci-source-info.json").write_text(json.dumps(metadata, indent=2) + "\n")
    print(f"Prepared core {revision} with reviewed patch {metadata['patch_sha256']}")


if __name__ == "__main__":
    main()
