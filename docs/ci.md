# 二进制包 CI

唯一工作流是 `.github/workflows/build-packages.yml`，以 **phono-fcitx5-addon 目录作为仓库根目录**。不要把包含其他模型项目的 collection 作为这个工作流的仓库根目录。

## 流程

1. 检出 addon，记录提交时间；版本标签必须与 `CMakeLists.txt` 的项目版本一致。
2. 按 `ci/core-source.json` 获取固定 core 提交并校验、应用双拼所需的补丁。按 `ci/core-dependencies.json` 固定 ExecuTorch 和 uni-algo。
3. 通过锁定的 `ci/pixi.toml`/`ci/pixi.lock`，使用 GCC 13.3 与 glibc 2.28 sysroot 编译一次共享 core，关闭按精度裁剪算子。运行 C ABI 测试，保留许可和构建来源。
4. 检查 core 的 ELF：GLIBC ≤ 2.28、GLIBCXX ≤ 3.4.31、CXXABI ≤ 1.3.14；无开发 RPATH，仅导出 Phono C ABI。将 core、头文件及许可打成中间 artifact。
5. 五个发行版容器并行安装原生 Fcitx/libime SDK，编译 addon 并运行七组 CTest。core 以独立 DSO 附带在 `fcitx5/phono/`，addon 使用 `$ORIGIN/phono` 寻找它。
6. 验证安装清单、翻译、许可和 ABI；生成原生包及二进制 tar.gz。Arch 的 makepkg 直接封装已验证的安装目录，不重复编译源码。
7. 在容器中安装实际原生包，以 Fcitx loader 加载它；测试不依赖 `LD_LIBRARY_PATH`。输出 SHA-256、ELF 报告、容器 digest 和核心构建来源。
8. PR/手动运行上传 Actions artifacts；匹配版本的标签运行在全部目标成功后上传 GitHub Release。

| 目标 | 原生包 | addon 的 glibc 上限 |
| --- | --- | --- |
| Ubuntu 24.04 | .deb | 2.39 |
| Debian 13 | .deb | 2.41 |
| Fedora 43 | .rpm | 2.42 |
| openSUSE Tumbleweed | .rpm | 构建容器的原生基线 |
| Arch Linux | .pkg.tar.zst | 构建容器的原生基线 |

core 的 glibc 2.28 基线不代表整个 addon 跨发行版兼容；Fcitx/libime 是 C++ ABI，必须下载匹配发行版的包。所有包均不附带 glibc、Pixi 环境或模型。CI 默认不下载真实模型，动态加载与原生集成测试走词典回退；有本地模型时可以设置 `PHONO_TEST_MODEL_DIR` 运行同一套模型集成测试。

## GitHub 上调用

将 addon 内容作为仓库根目录提交、推送，并在仓库启用 Actions。

- 手动：Actions → **Build downloadable Linux packages** → **Run workflow**。工作流需要先存在于默认分支。
- 自动：提交 PR 即运行。当前版本发布标签为 `v0.1.0`；修改版本后应使用对应 `v<version>` 标签。
- 命令行：在 addon 的 Git 仓库中执行 `gh workflow run build-packages.yml --ref <branch>`，再使用 `gh run list --workflow build-packages.yml` 和 `gh run download <run-id>`。

Actions 中间 artifact 为 `phono-core-prefix`；用户下载 `phono-packages-<target>`，其中包含原生包、二进制 tar.gz、校验和与来源报告。标签发布会汇总到 Release。安装方法见 README。

## 本地重跑同一链路

维护者需要 Pixi ≥ 0.81、Python、Git 和 Docker。以下命令均在 addon 根目录执行，使用新的构建目录；第一次需要网络下载固定依赖。不会安装到宿主桌面，原生包安装测试发生在容器里。

```sh
python3 tools/ci_prepare_core.py --output build/ci/core-source
pixi run --locked --manifest-path ci/pixi.toml \
  python tools/ci_build_core.py --source build/ci/core-source \
  --prefix build/ci/core-prefix --jobs 4
```

随后任选表中的目标，例如 openSUSE：

```sh
target=opensuse-tumbleweed
image=registry.opensuse.org/opensuse/tumbleweed:latest
docker pull "$image"
docker run --rm --platform linux/amd64 \
  -v "$PWD:/workspace" -w /workspace \
  -e TARGET="$target" -e MAX_GLIBC=native \
  "$image" bash -euc '
    bash ci/install-native-deps.sh "$TARGET"
    python3 tools/ci_build_packages.py --target "$TARGET" \
      --core-prefix /workspace/build/ci/core-prefix --max-glibc "$MAX_GLIBC" \
      --output "/workspace/dist/$TARGET"
  '
```

其他目标的镜像与 glibc 上限以工作流 matrix 为准。启用 SELinux 的宿主可对该隔离测试容器加 `--security-opt label=disable`，避免重标记项目目录。源码、构建及运行日志不应打进用户包；`.gitignore` 排除构建目录、SDK、环境及 `dist/`。完整在线运行无需宿主 Fcitx 开发包，也无需手工选择核心工具链。
