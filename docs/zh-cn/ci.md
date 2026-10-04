# 二进制包构建与发布

[English](../en-us/ci.md) · [安装与使用](../../README.md)

[Build downloadable Linux packages](../../.github/workflows/build-packages.yml) 工作流为五个 Linux 发行版构建 x86-64 二进制包，并验证安装后的动态加载。普通用户可直接下载，安装方法见 README。

## 下载产物

版本发布包位于 [GitHub Releases](https://github.com/phono-project/phono-fcitx5-addon/releases)。PR 和手动构建的测试包位于 [Actions](https://github.com/phono-project/phono-fcitx5-addon/actions/workflows/build-packages.yml) 对应运行的 artifacts 中。

| 发行版 | 目标 / artifact 后缀 | 原生包 |
| --- | --- | --- |
| Ubuntu 24.04 | `ubuntu2404` | `.deb` |
| Debian 13 | `debian13` | `.deb` |
| Fedora 43 | `fedora43` | `.rpm` |
| openSUSE Tumbleweed | `opensuse-tumbleweed` | `.rpm` |
| Arch Linux | `arch` | `.pkg.tar.zst` |

下载 `phono-packages-<target>`。每份 artifact 包含原生安装包、二进制 `.tar.gz`、SHA-256 校验和、ELF 版本需求报告和构建来源。`phono-core-prefix` 是构建中间产物，不是输入法安装包。

解压 artifact 后，可在该目录中验证文件完整性，例如：

```sh
sha256sum --check ubuntu2404-SHA256SUMS
```

安装包附带私有 `libphono_core.so` 和许可文件，模型需单独下载。包内不包含 Pixi 环境、开发 SDK 或 glibc。

## 运行与发布

- PR 自动触发构建。
- 手动运行：在 Actions 中打开 **Build downloadable Linux packages**，选择 **Run workflow**。该工作流需已存在于仓库默认分支。
- 版本发布：推送 `v<version>` 标签，版本必须与 `CMakeLists.txt` 中的项目版本一致。全部目标构建成功后，工作流将产物上传到对应 GitHub Release。

使用 GitHub CLI 手动运行并下载产物：

```sh
gh workflow run build-packages.yml --ref <branch>
gh run list --workflow build-packages.yml
gh run download <run-id> --name phono-packages-ubuntu2404
```

## 构建与兼容性

工作流先使用锁定的 Pixi GCC 13.3 和 glibc 2.28 sysroot 构建共享核心，关闭按精度裁剪算子，仅导出 Phono C ABI。核心源码和依赖版本分别由 `ci/core-source.json` 与 `ci/core-dependencies.json` 固定。

随后，各目标容器使用发行版原生 Fcitx5/libime SDK 独立构建 addon。CI 运行 CTest、检查安装布局和翻译、验证 ELF 版本需求，并安装实际原生包，通过 Fcitx5 loader 验证动态加载。核心安装在 addon 旁的 `phono/` 子目录，由 `$ORIGIN/phono` 定位；发行产物不允许开发环境 RPATH。

| 检查对象 | glibc 版本需求上限 |
| --- | --- |
| 共享核心 | 2.28 |
| Ubuntu 24.04 addon | 2.39 |
| Debian 13 addon | 2.41 |
| Fedora 43 addon | 2.42 |
| openSUSE / Arch addon | 对应构建容器的原生版本 |

核心的 GLIBCXX 与 CXXABI 版本需求上限分别为 3.4.31 和 1.3.14；addon 的上限由目标容器的原生 C++ 运行库决定。检查以 ELF 实际要求的符号版本为准。

核心的 glibc 2.28 基线不代表 addon 具备跨发行版的 Fcitx5/libime C++ ABI 兼容性，必须选择对应发行版的包。滚动发行版的构建容器 digest 保存在来源报告中。

CI 默认不下载模型，集成测试验证词典回退与动态加载。真实模型测试方法见 README 的开发部分。

## 本地复现

需要 Linux x86-64、Pixi ≥ 0.81、Python 3、Git 和 Docker。以下命令均在 addon 仓库根目录执行（包含 `CMakeLists.txt`、`ci/` 和 `tools/` 的目录）。首次运行需联网下载锁定依赖；宿主无需安装 Fcitx5 开发包，原生包安装测试在容器内进行。

先准备并构建共享核心：

```sh
python3 tools/ci_prepare_core.py --output build/ci/core-source
pixi run --locked --manifest-path ci/pixi.toml \
  python tools/ci_build_core.py --source build/ci/core-source \
  --prefix build/ci/core-prefix --jobs 4
```

再构建一个目标，例如 Ubuntu 24.04：

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

产物写入 `dist/ubuntu2404/`。其他目标的镜像和 `--max-glibc` 参数以工作流 matrix 为准；openSUSE 和 Arch 使用 `native`。本地复现同样执行安装布局、ELF 和原生包加载检查；构建失败时查看对应工具报告的错误。
