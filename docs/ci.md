# 构建与发布 / Builds and releases

[安装与使用 / Installation and usage](../README.md)

本仓库只构建和交付 Fcitx5 addon。core 的编译、算子支持及运行库 ABI 验证由 [phono-core](https://github.com/phono-project/phono-core) 负责；本仓库下载固定版本 SDK，并针对各发行版原生 Fcitx5/libime 编译。每份原生包分别适用于对应的发行版与架构。

This repository builds and delivers the Fcitx5 addon. [phono-core](https://github.com/phono-project/phono-core) owns core compilation, operator support, and runtime ABI validation. This repository downloads a pinned SDK and builds against each distribution’s native Fcitx5/libime. Each native package targets its own distribution and architecture.

## 执行路径 / Execution paths

PR 默认验证 Ubuntu 24.04 的 x86_64 与 aarch64，包括 CTest、词典回退、安装布局、ELF 需求及实际原生包加载。手动入口默认运行同一目标；将 `full_matrix` 设为 `true` 可运行完整五发行版、两架构矩阵，并在 Ubuntu 上验证固定模型的加载和真实推理，复现 Release 检查。Release 自动执行这些完整检查，缺失模型使推理检查失败。

Pull requests validate Ubuntu 24.04 on x86_64 and aarch64 by default, including CTest, dictionary fallback, installation layout, ELF requirements, and actual native-package loading. Manual runs use the same target by default. Set `full_matrix` to `true` to reproduce Release checks: the full five-distribution, two-architecture matrix and fixed-model loading and real inference on Ubuntu. Releases run these complete checks automatically. Missing models fail the inference check.

| 发行版 / Distribution | 目标 / Target | 原生包 / Native package | addon glibc 上限 / Addon glibc limit |
| --- | --- | --- | --- |
| Ubuntu 24.04 | `ubuntu2404` | `.deb` | 2.39 |
| Debian 13 | `debian13` | `.deb` | 2.41 |
| Fedora 43 | `fedora43` | `.rpm` | 2.42 |
| openSUSE Tumbleweed | `opensuse-tumbleweed` | `.rpm` | 构建容器原生版本 / Native container version |
| Arch Linux | `arch` | `.pkg.tar.zst` | 构建容器原生版本 / Native container version |

五个 Linux 目标均提供 x86_64 和实验性 aarch64 包，分别在 `ubuntu-24.04` / `ubuntu-24.04-arm` 原生 runner 的对应容器中构建。Arch 的 ARM64 目标使用社区 [Arch Linux ARM 镜像](https://github.com/Menci/docker-archlinuxarm)。core 额外检查 GCC 13 SDK 的 GLIBC ≤ 2.28、GLIBCXX ≤ 3.4.32、CXXABI ≤ 1.3.14、无 RPATH 和仅 C ABI 导出，再检查目标发行版的实际运行库是否满足需求。

All five Linux targets provide x86_64 and experimental aarch64 packages, built natively in matching containers on `ubuntu-24.04` / `ubuntu-24.04-arm`. ARM64 Arch uses the community [Arch Linux ARM image](https://github.com/Menci/docker-archlinuxarm). Core is checked for GLIBC ≤ 2.28, GLIBCXX ≤ 3.4.32, CXXABI ≤ 1.3.14, no RPATH, and C-only exports, followed by compatibility with the actual target runtime.

addon 的 GLIBCXX / CXXABI 上限来自目标容器的原生 C++ 运行库，检查以 ELF 实际引用的符号版本为准。core 的独立兼容说明随 SDK 发布。core 的兼容基线不能替代发行版的 Fcitx5/libime C++ ABI，因此始终选择对应发行版的安装包。

Addon GLIBCXX / CXXABI limits come from the target container’s native C++ runtime; checks inspect symbol versions actually referenced by the ELF binaries. The SDK includes core’s separate compatibility report. Core’s baseline does not replace distribution-specific Fcitx5/libime C++ ABI compatibility, so always select the package for your distribution.

## 依赖与模型 / Dependencies and models

`ci/core-sdk.json` 固定 core 版本与 SDK 资产。目前依赖 core v0.1.3 的 x86_64 / ARM64 SDK。`tools/fetch_core_sdk.py` 将下载缓存保存在 `.sdk/downloads`，安装到 `.sdk/phono-core`，并检查归档 SHA-256、SDK 版本、构建提交、库摘要及目标架构。首次使用前，固定版本的 core Release 必须已发布对应 SDK。

`ci/core-sdk.json` pins the core version and SDK asset. The current dependency is core v0.1.3, with separate x86_64 / ARM64 SDKs. `tools/fetch_core_sdk.py` caches downloads in `.sdk/downloads`, installs to `.sdk/phono-core`, and checks the archive SHA-256, installed version, source commit, library digest, and architecture. The pinned core Release must publish that SDK before first use.

SDK 提供 `include/phono-core/phono_api.h`、`lib/libphono_core.so`、`lib/cmake/phono-core` 和 `share/licenses/phono-core`，CMake 目标为 `phono::core`。普通提交更新 `ci/core-sdk.json` 后运行前端验证；core 发布不会自动触发 addon 发布。

The SDK provides `include/phono-core/phono_api.h`, `lib/libphono_core.so`, `lib/cmake/phono-core`, and `share/licenses/phono-core`, with the CMake target `phono::core`. Update `ci/core-sdk.json` in a normal commit and validate the frontend. Core releases do not automatically publish the addon.

Release 推理使用 `ci/model.json` 固定的 W8A8 模型修订，通过 `tools/fetch_model.py` 下载到本仓库 `.cache/models/release`，再以 `--model-dir` 传入打包脚本。用户可自行下载 README 中的 W4A8 或 W8A8 完整模型包；模型不放入安装包。

Release inference uses the W8A8 model revision pinned in `ci/model.json`. `tools/fetch_model.py` downloads it to this repository’s `.cache/models/release`, then passes it to the packaging script through `--model-dir`. Users may download either complete W4A8 or W8A8 package linked in the README. Models are separate from installation packages.

## 发布与下载 / Publishing and downloading

创建 `vX.Y.Z` 标签并发布对应 GitHub Release 后，`release.published` 检出该标签，验证标签版本与 CMake / Pixi 版本一致，确认该提交属于默认分支历史，然后构建、测试、打包、上传。正式版与预发布使用同一路径；仅推送标签不触发发布构建。只有上传任务具有写权限。

Create a `vX.Y.Z` tag and publish its GitHub Release. `release.published` checks out that tag, validates matching tag / CMake / Pixi versions, confirms the commit belongs to the default branch history, then builds, tests, packages, and uploads. Stable releases and prereleases use the same path. Pushing a tag alone does not start a release build. Only the upload job has write permission.

如果未来在工作流中自动创建 Release，应将构建和上传保留在同一工作流。三个项目使用各自版本，无需同步版本号。

If Release creation is later automated in a workflow, keep building and uploading in that same workflow. The three projects maintain independent versions and do not need synchronized version numbers.

安装包从 [GitHub Releases](https://github.com/phono-project/phono-fcitx5-addon/releases) 下载，构建产物保存在 [Actions](https://github.com/phono-project/phono-fcitx5-addon/actions/workflows/build-packages.yml) 的 `phono-fcitx5-addon-<arch>-<version>-<target>` artifact 中。每份只包含原生包和二进制 `.tar.gz`；ELF 检查结果输出到 CI 日志。安装方法见 README；CI 只上传二进制包，不修改 Release 正文。

Download installation packages from [GitHub Releases](https://github.com/phono-project/phono-fcitx5-addon/releases). Build artifacts are available in [Actions](https://github.com/phono-project/phono-fcitx5-addon/actions/workflows/build-packages.yml) as `phono-fcitx5-addon-<arch>-<version>-<target>`. Each contains only a native package and binary `.tar.gz`; ELF check results are printed in CI logs. See the README for installation instructions. CI only uploads binary packages and does not edit the Release body.

安装包将 core 放在 addon 旁的私有 `phono/` 目录，通过 `$ORIGIN/phono` 加载，并保留许可证。所有目标验证目录权限、翻译、安装后的动态加载和词典回退；发布产物不得携带开发环境 RPATH。

Packages place core in a private `phono/` directory beside the addon, load it through `$ORIGIN/phono`, and retain licenses. Every target validates directory permissions, translations, installed dynamic loading, and dictionary fallback. Release artifacts must contain no development-environment RPATH.

## 本地复现 / Local reproduction

需要 Linux x86-64 或 ARM64、Python 3、Git 和 Docker；日常源码测试另需 README 中的 Pixi 与宿主开发包。以下命令在独立 addon 仓库根目录运行，首次运行下载固定 SDK，宿主不需安装容器内的 Fcitx5 开发包。

You need Linux x86-64 or ARM64, Python 3, Git, and Docker; routine source tests also require Pixi and the host development packages listed in the README. Run these commands from an independent addon checkout. The first run downloads the pinned SDK, and the host does not need the container’s Fcitx5 development packages.

```sh
python3 tools/fetch_core_sdk.py
docker run --rm --platform linux/amd64 -v "$PWD:/workspace" -w /workspace ubuntu:24.04 bash -euc '
  bash ci/install-native-deps.sh ubuntu2404
  python3 tools/ci_build_packages.py --target ubuntu2404 --core-prefix /workspace/.sdk/phono-core --max-glibc 2.39 --output /workspace/dist/ubuntu2404-x86_64
'
```

加入真实模型验证时先下载固定模型，并向同一打包脚本增加 `--require-model --model-dir /workspace/.cache/models/release`。参数指定的模型目录缺失或不完整时立即失败。

For real-model validation, download the pinned model first and add `--require-model --model-dir /workspace/.cache/models/release` to the same packaging command. A missing or incomplete model directory fails immediately.

```sh
python3 tools/fetch_model.py --output .cache/models/release
```

产物位于 `dist/ubuntu2404-x86_64/`。其他目标的镜像及参数见工作流 matrix。手动全量检查可在 Actions 勾选 `full_matrix`，或使用 GitHub CLI：

Outputs are written to `dist/ubuntu2404-x86_64/`. See the workflow matrix for other targets’ images and arguments. Run a manual full check by selecting `full_matrix` in Actions or using the GitHub CLI:

```sh
gh workflow run build-packages.yml --ref <branch> -f full_matrix=true
gh run list --workflow build-packages.yml
gh run download <run-id> --name "phono-fcitx5-addon-x86_64-<version>-ubuntu2404"
```
