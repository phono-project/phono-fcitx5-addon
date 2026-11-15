# macOS ARM64（实验性）/ macOS ARM64 (experimental)

需要 macOS 14 或更新版本、Apple Silicon，以及 [Fcitx5 macOS](https://github.com/fcitx-contrib/fcitx5-macos) 和它的 Chinese Addons（中文插件）。Phono 不能作为独立 macOS 输入法启动。先在 Fcitx5 的插件管理器中安装中文插件；它提供拼音词典、libime 语言模型、标点和全角模块，即使不使用其拼音输入法也需要保留。

Requires macOS 14 or later, Apple Silicon, [Fcitx5 macOS](https://github.com/fcitx-contrib/fcitx5-macos), and its Chinese Addons plugin. Phono runs inside Fcitx5. Install Chinese Addons through Fcitx5's plugin manager first; its dictionary, language model, punctuation, and full-width modules remain required even if you do not use its Pinyin engine.

从本项目 Release 下载 `phono-fcitx5-addon-arm64-<version>-macos.tar.bz2`。退出 Fcitx5 后，将归档解压到插件目录：

Download `phono-fcitx5-addon-arm64-<version>-macos.tar.bz2` from this project's Release. Quit Fcitx5, then extract into its plugin directory:

```sh
mkdir -p "$HOME/Library/fcitx5"
tar -xjf phono-fcitx5-addon-arm64-<version>-macos.tar.bz2 -C "$HOME/Library/fcitx5"
```

重新打开 Fcitx5，在输入法列表中添加「Phono - 拼音输入」或「Phono - 双拼输入」。按照 [README](../README.md) 下载完整模型，并在 Phono 配置中设置模型包目录。模型不在归档中。

Reopen Fcitx5 and add “Phono - Pinyin Input” or “Phono - Shuangpin Input”. Download a complete model as described in the [README](../README.md) and select its directory in Phono settings. Models are not included in the archive.

包内包含 `lib/fcitx5/libphono.so`、私有 `lib/fcitx5/phono/` 动态库、`share/` 下的数据、翻译与许可证，以及 `plugin/phono.json` 文件清单。安装不会覆盖其他中文插件的数据。更新时使用同样的解压步骤；卸载时退出 Fcitx5，依据 `plugin/phono.json` 清单删除本插件文件及描述文件；保留个人配置、用户词典和独立保存的模型。

The archive contains `lib/fcitx5/libphono.so`, private libraries under `lib/fcitx5/phono/`, data/translations/licenses under `share/`, and the `plugin/phono.json` manifest. It does not replace Chinese Addons data. Update by extracting the new archive in the same way. To uninstall, quit Fcitx5 and remove this plugin’s files listed in `plugin/phono.json`, then its descriptor. Retain personal settings, learned vocabulary, and separately stored models.

## 构建与验证 / Build and verification

唯一发布工作流使用原生 `macos-26` ARM64 runner。macOS runner 是虚拟机，不使用 Linux Docker 容器。`ci/macos-sdk.json` 锁定 Fcitx5 官方开发 SDK、中文插件、libime / KenLM 源码和预构建依赖的 SHA-256。Pixi 管理构建工具，Xcode 的 Apple Clang 与系统 libc++ 负责目标 ABI；Homebrew 仅提供 ECM 构建元数据。

The single delivery workflow uses the native `macos-26` ARM64 runner, a macOS VM rather than a Linux Docker container. `ci/macos-sdk.json` pins the official Fcitx5 development SDK, Chinese Addons, libime / KenLM sources, and prebuilt dependencies by SHA-256. Pixi supplies build tools; Xcode Apple Clang and system libc++ supply the target ABI. Homebrew supplies ECM build metadata only.

macOS 的 libime 私有库使用独立安装名，通过 `@loader_path` 加载；core 从独立 GitHub Release 下载，继续动态链接其 C ABI。SDK、构建环境、中文插件模型和 Phono 模型不打入包。归档前检查 Mach-O 架构、导入和加载路径，再将实际归档解压到另一目录，运行已安装模块的词典回退测试；Release / 全量手动构建另用固定模型运行同一个加载测试。

Private libime libraries use unique install names and `@loader_path` searches. Core comes from its independent GitHub Release and remains linked through its shared C ABI. SDKs, build environments, Chinese Addons model data, and Phono models are excluded. CI audits Mach-O architecture/imports/loader paths, then extracts the actual archive elsewhere and tests installed dictionary fallback. Releases and full manual runs reuse the same loading test with the pinned model.

归档布局参考官方插件的[打包脚本](https://github.com/fcitx-contrib/fcitx5-plugins/blob/master/scripts/install.sh)。这里将二进制与架构无关数据合并为一份独立下载的 `.tar.bz2`，供手动安装；不自动注册到官方插件仓库。当前分发为实验性，最终可用性以对应 CI 和用户实测为准。

The layout follows the official plugin [packaging script](https://github.com/fcitx-contrib/fcitx5-plugins/blob/master/scripts/install.sh). Binary and architecture-independent data are combined into one standalone `.tar.bz2` for manual installation; this does not register Phono in the official plugin catalog. Distribution is experimental; consult the corresponding CI results and user testing.
