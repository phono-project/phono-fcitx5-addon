# Phono Fcitx5 addon

Phono 是 Linux 上的 Fcitx5 中文输入法，提供「Phono - 拼音输入」和「Phono - 双拼输入」。它结合 Phono 模型的整句候选与 libime 词典候选，根据上文生成中文，并支持逐词选择。

Phono is a Chinese input method for Fcitx5 on Linux, with “Phono - Pinyin Input” and “Phono - Shuangpin Input”. It combines sentence candidates from the Phono model with libime dictionary candidates, using preceding text to generate Chinese and supporting word-by-word selection.

## 安装 / Installation

从 [GitHub Releases](https://github.com/phono-project/phono-fcitx5-addon/releases) 下载对应发行版的 **x86-64 二进制包**。测试包位于 [Actions](https://github.com/phono-project/phono-fcitx5-addon/actions/workflows/build-packages.yml) 成功运行的 `phono-fcitx5-addon-<arch>-<version>-<platform>` artifact 中，下载后先解压。

Download an **x86-64 binary package** for your distribution from [GitHub Releases](https://github.com/phono-project/phono-fcitx5-addon/releases). Test builds are available in the `phono-fcitx5-addon-<arch>-<version>-<platform>` artifacts of successful [Actions runs](https://github.com/phono-project/phono-fcitx5-addon/actions/workflows/build-packages.yml); extract the downloaded artifact first.

| 发行版 / Distribution | Actions artifact | 安装命令 / Install command |
| --- | --- | --- |
| Ubuntu 24.04 | `phono-fcitx5-addon-x86_64-<version>-ubuntu2404` | `sudo apt install ./phono-fcitx5-addon-x86_64-*.deb` |
| Debian 13 | `phono-fcitx5-addon-x86_64-<version>-debian13` | `sudo apt install ./phono-fcitx5-addon-x86_64-*.deb` |
| Fedora 43 | `phono-fcitx5-addon-x86_64-<version>-fedora43` | `sudo dnf install ./phono-fcitx5-addon-x86_64-*.rpm` |
| openSUSE Tumbleweed | `phono-fcitx5-addon-x86_64-<version>-opensuse-tumbleweed` | `sudo zypper install ./phono-fcitx5-addon-x86_64-*.rpm` |
| Arch Linux | `phono-fcitx5-addon-x86_64-<version>-arch` | `sudo pacman -U ./phono-fcitx5-addon-x86_64-*.pkg.tar.zst` |

包名格式为 `phono-fcitx5-addon-<arch>-<version>-<platform>.<ext>`，例如 `phono-fcitx5-addon-x86_64-0.1.0-fedora43.rpm`。Actions artifact 使用同一名称，不带扩展名；`<version>` 为实际版本号。

Package filenames follow `phono-fcitx5-addon-<arch>-<version>-<platform>.<ext>`, for example `phono-fcitx5-addon-x86_64-0.1.0-fedora43.rpm`. Actions artifacts use the same name without an extension; replace `<version>` with the actual version.

在下载目录运行对应命令。即使扩展名相同，也应选择为当前发行版构建的包。安装包包含私有的 phono-core 运行库及许可证，模型需单独下载；Release 的下载指引和各发行版来源报告记录所用 core 版本。

Run the corresponding command in the download directory. Choose the package built for your distribution, even when another package has the same extension. Packages include a private phono-core runtime and its licenses; download the model separately. The Release download guide and distribution provenance reports identify the core version used.

### 下载模型并启用 / Download a model and enable Phono

1. 下载并保存完整的 v2.2 模型包：[w4a8](https://huggingface.co/afirelily/phonop2c_v2_2_base_w4a8_model) 或 [w8a8](https://huggingface.co/afirelily/phonop2c_v2_2_base_w8a8_model)。如已安装 Hugging Face CLI，可执行：

   Download either complete v2.2 model package: [w4a8](https://huggingface.co/afirelily/phonop2c_v2_2_base_w4a8_model) or [w8a8](https://huggingface.co/afirelily/phonop2c_v2_2_base_w8a8_model). If you have the Hugging Face CLI installed, run:

   ```sh
   hf download afirelily/phonop2c_v2_2_base_w4a8_model --local-dir ./phonop2c_v2_2_base_w4a8_model
   # 或选择 w8a8 / Or choose w8a8
   hf download afirelily/phonop2c_v2_2_base_w8a8_model --local-dir ./phonop2c_v2_2_base_w8a8_model
   ```

2. 重新启动 Fcitx5，打开桌面的 Fcitx5 配置工具，添加「Phono - 拼音输入」或「Phono - 双拼输入」。KDE Wayland 下建议注销并重新登录。

   Restart Fcitx5, open your desktop’s Fcitx5 configuration tool, and add “Phono - Pinyin Input” or “Phono - Shuangpin Input”. On KDE Wayland, logging out and back in is recommended.

3. 打开 Phono 配置，将「模型包目录」设为下载目录的绝对路径。该目录应直接包含 `config.json`、`bins/` 和 `vocabs/`。保持「启用 Phono 模型」开启；使用双拼时，选择自己的双拼方案（默认小鹤）。

   In Phono settings, set “Model package directory” to the absolute path of the downloaded directory. It must directly contain `config.json`, `bins/`, and `vocabs/`. Leave “Enable Phono model” enabled. For Shuangpin, select your scheme (Xiaohe by default).

> 建议将模型保存在固定目录，移动后需更新「模型包目录」。
>
> Keep the model in a permanent directory; update “Model package directory” if you move it.

模型未配置、被禁用或加载失败时，仍可使用词典候选。配置在 Fcitx5 中保存后生效，无需修改模型包内的 `config.json`。

Dictionary candidates remain available when the model is unconfigured, disabled, or fails to load. Save settings in Fcitx5 to apply them; you do not need to edit the model package’s `config.json`.

Phono 配置通常保存在 `~/.config/fcitx5/conf/phono.conf`，用户词典和历史在 `~/.local/share/fcitx5/phono/`；设置了 `XDG_CONFIG_HOME` 或 `XDG_DATA_HOME` 时使用对应目录。模型目录由用户选择，不属于这些配置目录，也不会随安装包更新。

Phono settings normally live in `~/.config/fcitx5/conf/phono.conf`, with the user dictionary and history under `~/.local/share/fcitx5/phono/`. `XDG_CONFIG_HOME` and `XDG_DATA_HOME` override these locations. The model directory is selected by the user, is separate from these settings, and is not replaced by package updates.

## 使用 / Usage

「显示预编辑框」默认关闭，不在应用输入框中显示尚未提交的文字；候选窗仍显示拼音。开启后，支持内嵌预编辑的应用会在输入框中显示预编辑文本，拼音不再重复显示在候选窗；不支持的应用仍在候选窗中显示拼音。

“Show preedit” is disabled by default, hiding uncommitted text in the application input field while keeping pinyin in the candidate window. When enabled, applications that support embedded preedit display the text in the input field instead of duplicating the pinyin in the candidate window; other applications continue to show it in the candidate window.

输入拼音后，词典候选立即显示；模型生成的整句候选随后排在前面，通常为 3 个。与模型候选文本重复的词典项会去除，其余词典候选仍可选择。浏览候选期间，列表停止异步替换。

Dictionary candidates appear immediately as you type. Model sentence candidates are added at the front when ready, usually three. Dictionary entries with the same text as model candidates are omitted; the remaining dictionary candidates stay available. The list stops updating asynchronously while you browse it.

选择模型候选会提交整句。选择词典候选后，可以继续逐词选择；本轮提交或清空后恢复模型候选。将拼音光标移到中间编辑时，使用词典候选。

Selecting a model candidate commits the sentence. Selecting a dictionary candidate lets you continue word by word; model candidates resume after the composition is committed or cleared. Editing with the pinyin cursor in the middle uses dictionary candidates.

| 按键 / Key | 操作 / Action |
| --- | --- |
| 数字键 / Number keys | 选择对应候选<br>Select the corresponding candidate |
| Space | 确认当前候选<br>Confirm the current candidate |
| `=`、`+`、Tab、PageDown | 下一页<br>Next page |
| `-`、`_`、Shift+Tab、PageUp | 上一页<br>Previous page |
| ↑ / ↓ | 移动候选光标<br>Move the candidate cursor |
| ← / →、Home / End | 移动拼音光标<br>Move the pinyin cursor |
| Backspace / Delete | 删除拼音<br>Delete pinyin |
| Delete（高亮用户词时） | 删除该用户词并重新生成候选<br>Forget the highlighted user word and regenerate candidates |
| Enter | 提交已选文字及剩余原始输入<br>Commit selected text and the remaining raw input |
| Escape | 取消当前输入<br>Cancel the composition |

双拼支持自然码、微软、紫光、智能 ABC、中文之星、拼音加加和小鹤，按所选方案转换音节，不自动猜测或纠正误键。无法组成合法双拼的两键会拆开，保留可识别的音节或简拼；无法识别的字符按「非法输入」配置处理：默认原样保留，也可选择以删除线标出并在提交时去除。

Shuangpin supports Natural Code, Microsoft, Purple Light, Smart ABC, Chinese Star, Pinyin Jiajia, and Xiaohe. It converts syllables using the selected scheme without guessing or correcting mistyped keys. Invalid two-key pairs are split to retain recognizable syllables or initials. Unrecognized characters follow the “Invalid input” setting: pass through unchanged by default, or show struck-out text that is removed on commit.

中文标点和全角字符通过 Fcitx5 的原生功能处理。词典选词可学习，Phono 的用户词典独立保存；模型候选不参与词典学习。

Chinese punctuation and full-width characters use Fcitx5’s native features. Dictionary selections can be learned and are stored in Phono’s separate user dictionary; model candidates do not contribute to dictionary learning.

「仅精确匹配时优先用户词」默认开启：用户词只匹配输入前缀、简拼或未完成音节时，排在模型结果之后。关闭后可优先显示前缀用户词；多字输入中的单字记忆始终不前置。高亮用户词后按 Delete 可删除记忆并重新生成候选，全拼和双拼共用学习记录。

“Prioritize user words only for exact matches” is enabled by default. User words matching a prefix, abbreviation, or unfinished syllable follow model results. Disabling it allows prefix user words to lead; single-character memories never receive priority for longer input. Highlight a user word and press Delete to forget it and regenerate candidates. Pinyin and shuangpin share learning records.

「启用用户词」和「用户词排在模型结果之前」默认开启。关闭前者后，不使用用户词和记忆排序，保留已有记录；关闭后者后，用户词排在模型结果之后。「学习词典选择」独立控制是否继续学习新词。

“Enable user words” and “Show user words before model results” are enabled by default. Disable the former to stop using remembered vocabulary and ranking while retaining saved records; disable the latter to place user words after model results. “Learn from dictionary selections” independently controls new learning.

## 上文与隐私 / Context and privacy

Phono 优先读取应用提供的光标前文本，以生成符合上文的候选；可在配置中关闭「使用应用提供的上文」或调整读取字符数。应用不提供有效上文时，只使用当前输入上下文中连续提交的历史。

Phono prefers text before the cursor supplied by the application to generate context-aware candidates. You can disable “Use surrounding text from applications” or change the character limit in settings. When the application provides no valid surrounding text, Phono uses the history of consecutive commits in the current input context.

部分应用不报告鼠标移动后的光标位置，Phono 因而无法获取新位置的上文，候选可能与实际内容不符。密码及敏感输入框中禁用模型推理、上文记录和词典学习。诊断日志不记录输入文本。

Some applications do not report the cursor position after mouse navigation, so Phono cannot obtain the preceding text at the new position and candidates may reflect stale context. Model inference, context recording, and dictionary learning are disabled in password and sensitive fields. Diagnostic logs do not record input text.

## 卸载 / Uninstallation

使用对应包管理器卸载 `fcitx5-phono`：

Remove `fcitx5-phono` with your distribution’s package manager:

```sh
sudo apt remove fcitx5-phono       # Ubuntu / Debian
sudo dnf remove fcitx5-phono       # Fedora
sudo zypper remove fcitx5-phono    # openSUSE
sudo pacman -R fcitx5-phono        # Arch Linux
```

卸载后重新启动 Fcitx5。模型、个人配置和用户词典会保留。

Restart Fcitx5 after removal. Models, personal settings, and the user dictionary are retained.

## 开发 / Development

本地构建需要 Linux、Pixi ≥ 0.81、支持 C++20 的宿主编译器、与宿主匹配的 Fcitx5 ≥ 5.1.7 / libime ≥ 1.1.5 开发包，以及 Fcitx5 标点、全角模块。本仓库独立构建，使用 `ci/core-sdk.json` 固定的 phono-core Release SDK，不需要 core 源码或相邻仓库。

Local builds require Linux, Pixi ≥ 0.81, a host compiler with C++20 support, Fcitx5 ≥ 5.1.7 / libime ≥ 1.1.5 development packages matching the host, and the Fcitx5 punctuation and full-width modules. This repository builds independently against the phono-core Release SDK pinned in `ci/core-sdk.json`, without core sources or adjacent repositories.

在本项目目录执行：

Run these commands in this project’s directory:

```sh
pixi install --locked
pixi run fetch-core
pixi run test
# 使用显式模型目录验证推理 / Test inference with an explicit model directory
PHONO_TEST_MODEL_DIR=/absolute/path/to/model pixi run test
```

SDK 下载到 `.sdk/phono-core`，下载程序检查 SDK 内的版本与固定版本一致。测试的配置和用户词典写入构建目录内的隔离环境，日常测试无需模型。使用其他已安装 SDK 时可执行 `PHONO_SDK_PREFIX=/absolute/path/to/sdk pixi run test`；直接调用 CMake 时使用 `-DPHONO_SDK_PREFIX=...` 或 `CMAKE_PREFIX_PATH`。

The SDK is downloaded to `.sdk/phono-core`. The downloader checks that the installed SDK version matches the pinned version. Tests write settings and user dictionaries to isolated environments inside the build directory; routine tests need no model. To use another installed SDK, run `PHONO_SDK_PREFIX=/absolute/path/to/sdk pixi run test`. Direct CMake builds accept `-DPHONO_SDK_PREFIX=...` or `CMAKE_PREFIX_PATH`.

显式本地覆盖仍支持 `PHONO_CORE_INCLUDE_DIR`、`PHONO_CORE_LIBRARY` 和打包所需的 `PHONO_CORE_LICENSE_ROOT`，通过 `tools/configure.py` 的 `-D` 参数传入。二进制包构建、Release 规则和安装验证见 [CI 文档](docs/ci.md)。贡献代码前请运行适合改动的测试；涉及发行版打包时，可手动运行完整矩阵。

Explicit local overrides still accept `PHONO_CORE_INCLUDE_DIR`, `PHONO_CORE_LIBRARY`, and `PHONO_CORE_LICENSE_ROOT` for packaging, passed as `-D` arguments to `tools/configure.py`. See the [CI documentation](docs/ci.md) for package builds, Release rules, and installation checks. Run tests relevant to your contribution; packaging changes can be checked with a manual full-matrix run.

## 许可证 / License

本项目使用 [Apache License 2.0](LICENSE)。

This project is licensed under the [Apache License 2.0](LICENSE).
