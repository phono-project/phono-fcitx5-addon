#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Runs only inside the CI target container, never on the end user's desktop.
set -euo pipefail
case "${1:?target id required}" in
    debian13 | ubuntu2404)
        export DEBIAN_FRONTEND=noninteractive
        apt-get update
        apt-get install -y --no-install-recommends \
            ca-certificates git build-essential cmake ninja-build gettext python3 \
            pkg-config libboost-dev libfcitx5core-dev libimecore-dev libimepinyin-dev libime-data \
            libime-data-language-model nlohmann-json3-dev fcitx5 \
            fcitx5-module-punctuation fcitx5-module-fullwidth \
            dpkg-dev binutils file xz-utils
        ;;
    fedora43)
        dnf install -y --setopt=install_weak_deps=False ca-certificates git gcc-c++ cmake ninja-build gettext python3 \
            pkgconf-pkg-config boost-devel fcitx5-devel libime-devel libime-data \
            json-devel fcitx5 fcitx5-chinese-addons rpm-build binutils file xz
        ;;
    opensuse-tumbleweed)
        zypper --non-interactive refresh
        zypper --non-interactive install --no-recommends \
            ca-certificates git gcc-c++ cmake ninja gettext-tools python3 pkgconf \
            boost-devel fcitx5-devel libime-devel libime-dicts nlohmann_json-devel \
            fcitx5 fcitx5-chinese-addons rpm-build binutils file xz
        ;;
    arch)
        pacman -Syu --noconfirm --needed base-devel ca-certificates git cmake ninja \
            gettext python pkgconf boost fcitx5 libime nlohmann-json \
            fcitx5-chinese-addons binutils file xz
        ;;
    *) printf 'Unsupported target: %s\n' "$1" >&2; exit 2 ;;
esac
