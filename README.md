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

在解压后的目录运行对应命令。即使扩展名相同，也应选择为当前发行版构建的包。安装包包含推理库，模型需单独下载。

Run the corresponding command in the extracted directory. Choose the package built for your distribution, even when another package has the same extension. The inference library is included; download the model separately.

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

## 使用 / Usage

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
| Enter | 提交已选文字及剩余原始输入<br>Commit selected text and the remaining raw input |
| Escape | 取消当前输入<br>Cancel the composition |

双拼支持自然码、微软、紫光、智能 ABC、中文之星、拼音加加和小鹤，按所选方案转换音节，不自动猜测或纠正误键。无法组成合法双拼的两键会拆开，保留可识别的音节或简拼；无法识别的字符按「非法输入」配置处理：默认原样保留，也可选择以删除线标出并在提交时去除。

Shuangpin supports Natural Code, Microsoft, Purple Light, Smart ABC, Chinese Star, Pinyin Jiajia, and Xiaohe. It converts syllables using the selected scheme without guessing or correcting mistyped keys. Invalid two-key pairs are split to retain recognizable syllables or initials. Unrecognized characters follow the “Invalid input” setting: pass through unchanged by default, or show struck-out text that is removed on commit.

中文标点和全角字符通过 Fcitx5 的原生功能处理。词典选词可学习，Phono 的用户词典独立保存；模型候选不参与词典学习。

Chinese punctuation and full-width characters use Fcitx5’s native features. Dictionary selections can be learned and are stored in Phono’s separate user dictionary; model candidates do not contribute to dictionary learning.

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

本地构建需要 Linux、Pixi ≥ 0.81、支持 C++20 的宿主编译器、与宿主匹配的 Fcitx5 ≥ 5.1.7 / libime ≥ 1.1.5 开发包，以及 Fcitx5 标点、全角模块。还需准备 phono-core 的 C ABI 头文件与共享库；默认从相邻的 `../phono-core/interface` 和 `../phono-core/results/libphono_core.so` 查找。

Local builds require Linux, Pixi ≥ 0.81, a host compiler with C++20 support, Fcitx5 ≥ 5.1.7 / libime ≥ 1.1.5 development packages matching the host, and the Fcitx5 punctuation and full-width modules. Supply the phono-core C ABI headers and shared library; the defaults are the adjacent `../phono-core/interface` and `../phono-core/results/libphono_core.so`.

在本项目目录执行：

Run these commands in this project’s directory:

```sh
pixi install --locked
pixi run test
# 使用真实模型验证推理与动态加载 / Test inference and dynamic loading with a real model
PHONO_TEST_MODEL_DIR=/absolute/path/to/model pixi run test
```

测试的配置和用户词典写入构建目录内的隔离环境。自定义核心路径可通过 `pixi run python tools/configure.py -DPHONO_CORE_INCLUDE_DIR=/path/to/include -DPHONO_CORE_LIBRARY=/path/to/libphono_core.so` 指定。

Tests write settings and user dictionaries to isolated environments inside the build directory. To use a different core location, run `pixi run python tools/configure.py -DPHONO_CORE_INCLUDE_DIR=/path/to/include -DPHONO_CORE_LIBRARY=/path/to/libphono_core.so`.

二进制包构建与安装验证见 [CI 文档](docs/zh-cn/ci.md)。

See the [CI documentation](docs/en-us/ci.md) for binary package builds and installation checks.

## 许可证 / License

本项目使用 [Apache License 2.0](LICENSE)。

This project is licensed under the [Apache License 2.0](LICENSE).
