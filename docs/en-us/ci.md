# Binary package builds and releases

[简体中文](../zh-cn/ci.md) · [Installation and usage](../../README.md)

The [Build downloadable Linux packages](../../.github/workflows/build-packages.yml) workflow builds x86-64 binary packages for five Linux distributions and checks dynamic loading after installation. Users can download the packages directly; see the README for installation instructions.

## Downloads

Release packages are available on [GitHub Releases](https://github.com/phono-project/phono-fcitx5-addon/releases). Artifacts from pull requests, manual builds, and published releases are available in the artifacts of the corresponding [Actions run](https://github.com/phono-project/phono-fcitx5-addon/actions/workflows/build-packages.yml).

| Distribution | Target / artifact suffix | Native package |
| --- | --- | --- |
| Ubuntu 24.04 | `ubuntu2404` | `.deb` |
| Debian 13 | `debian13` | `.deb` |
| Fedora 43 | `fedora43` | `.rpm` |
| openSUSE Tumbleweed | `opensuse-tumbleweed` | `.rpm` |
| Arch Linux | `arch` | `.pkg.tar.zst` |

Download `phono-fcitx5-addon-<arch>-<version>-<platform>`. Each artifact contains a native package, a binary `.tar.gz`, SHA-256 checksums, an ELF version requirements report, and build provenance. `phono-core-prefix` is an intermediate build artifact, not an input method package.

Package filenames use the same name with a `.deb`, `.rpm`, `.pkg.tar.zst`, or `.tar.gz` extension. The current architecture is `x86_64`; replace `<version>` with the actual version and `<platform>` with a target from the table.

After extracting the artifact, verify file integrity in that directory, for example:

```sh
sha256sum --check ubuntu2404-SHA256SUMS
```

Packages include a private `libphono_core.so` and license files. Download models separately. Packages do not contain a Pixi environment, development SDK, or glibc.

## Running builds and publishing releases

- Pull requests trigger builds automatically.
- To run a build manually, open **Build downloadable Linux packages** in Actions and select **Run workflow**. The workflow must already exist on the repository’s default branch.
- To publish a release, create a `v<version>` tag and publish its GitHub Release. The version must match the project version in the tagged `CMakeLists.txt`. The Release `published` event starts the build, including prereleases. Once all targets succeed, the workflow uploads binaries and reports to that Release and retains the Actions artifacts. Pushing a tag alone or saving a draft does not start a build.

Release builds check out the release tag; manual and pull request builds use their selected refs. All triggers share one build workflow. Publishing a Release with another workflow’s `GITHUB_TOKEN` does not trigger a subsequent Release workflow. Publish through the web UI or CLI, or use a GitHub App / PAT that can trigger workflows. See the [GitHub trigger rules](https://docs.github.com/en/actions/how-tos/write-workflows/choose-when-workflows-run/trigger-a-workflow).

Use the GitHub CLI to start a manual build and download its artifacts:

```sh
gh workflow run build-packages.yml --ref <branch>
gh run list --workflow build-packages.yml
gh run download <run-id> --name "phono-fcitx5-addon-x86_64-<version>-ubuntu2404"
```

## Build process and compatibility

The workflow first builds the shared core with the locked Pixi GCC 13.3 toolchain and glibc 2.28 sysroot, disables dtype-selective operator builds, and exports only the Phono C ABI. Core source and dependency revisions are pinned in `ci/core-source.json` and `ci/core-dependencies.json`, respectively.

Each target container then builds the addon independently against its distribution’s native Fcitx5/libime SDK. CI runs CTest, checks installation layout and translations, verifies ELF version requirements, installs the actual native package, and checks dynamic loading through the Fcitx5 loader. The core is installed in a `phono/` subdirectory beside the addon and located through `$ORIGIN/phono`. Release artifacts must not contain development environment RPATHs.

| Binary checked | Maximum required glibc version |
| --- | --- |
| Shared core | 2.28 |
| Ubuntu 24.04 addon | 2.39 |
| Debian 13 addon | 2.41 |
| Fedora 43 addon | 2.42 |
| openSUSE / Arch addon | Native version in the corresponding build container |

The core’s GLIBCXX and CXXABI requirements are capped at 3.4.31 and 1.3.14, respectively. Addon limits come from the target container’s native C++ runtime. Checks use the symbol versions actually required by the ELF binaries.

The core’s glibc 2.28 baseline does not establish cross-distribution compatibility with the Fcitx5/libime C++ ABI. Choose the package for your distribution. Build provenance records the container digest for rolling distributions.

CI does not download models by default; integration tests exercise dictionary fallback and dynamic loading. See the README’s development section for tests with a real model.

## Reproducing builds locally

You need Linux x86-64, Pixi ≥ 0.81, Python 3, Git, and Docker. Run the commands below from the addon repository root, which contains `CMakeLists.txt`, `ci/`, and `tools/`. The first run downloads pinned dependencies. The host does not need Fcitx5 development packages; native package installation tests run inside the container.

Prepare and build the shared core first:

```sh
python3 tools/ci_prepare_core.py --output build/ci/core-source
pixi run --locked --manifest-path ci/pixi.toml \
  python tools/ci_build_core.py --source build/ci/core-source \
  --prefix build/ci/core-prefix --jobs 4
```

Then build a target, for example Ubuntu 24.04:

```sh
target=ubuntu2404
image=ubuntu:24.04
max_glibc=2.39
docker pull --platform linux/amd64 "$image"
digest=$(docker image inspect "$image" --format '{{index .RepoDigests 0}}')
docker run --rm --platform linux/amd64 \
  -v "$PWD:/workspace" -w /workspace \
  -e "TARGET=$target" -e "MAX_GLIBC=$max_glibc" \
  -e "CONTAINER_DIGEST=$digest" \
  -e "SOURCE_DATE_EPOCH=$(git show -s --format=%ct HEAD)" \
  "$digest" bash -euc '
    bash ci/install-native-deps.sh "$TARGET"
    python3 tools/ci_build_packages.py --target "$TARGET" \
      --core-prefix /workspace/build/ci/core-prefix --max-glibc "$MAX_GLIBC" \
      --output "/workspace/dist/$TARGET"
  '
```

Outputs are written to `dist/ubuntu2404/`. For other targets, use the image and `--max-glibc` value from the workflow matrix; openSUSE and Arch use `native`. Local builds run the same installation layout, ELF, and native package loading checks. If a build fails, inspect the error reported by the corresponding tool.
