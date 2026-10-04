# Phono Fcitx5 addon

「Phono - 拼音输入」与「Phono - 双拼输入」通过动态链接 phono-core，在 Fcitx5 同一进程的工作线程中推理。主线程使用 libime 处理拼音编辑、词典候选和拆词；新输入取消旧推理，不通过 IPC 启动服务。

## 安装与卸载

从 Actions artifacts 或 GitHub Release 下载匹配发行版的 x86-64 二进制包。支持 Ubuntu 24.04、Debian 13、Fedora 43、openSUSE Tumbleweed、Arch Linux。包内附带私有 `libphono_core.so`，不附带模型，也不需要用户编译。

```sh
# 在下载目录执行对应发行版的命令
sudo apt install ./fcitx5-phono-*.deb
sudo dnf install ./fcitx5-phono-*.rpm
sudo zypper install ./fcitx5-phono-*.rpm
sudo pacman -U ./fcitx5-phono-*.pkg.tar.zst
```

更新同版本 openSUSE 测试包可使用 `zypper install --force`。安装后通过原桌面启动入口重新加载 Fcitx；KDE Wayland 可重新登录，避免托盘重启丢失 KWin 传入的 socket。添加 Phono 输入法，设置包含 `config.json`、`bins/`、`vocabs/` 的模型目录。

卸载分别使用 `apt remove fcitx5-phono`、`dnf remove fcitx5-phono`、`zypper remove fcitx5-phono` 或 `pacman -R fcitx5-phono`。它们不删除模型、用户词典或用户配置。

直接安装测试使用构建后的 `sudo cmake --install build/pixi`。卸载时按 `build/pixi/install_manifest.txt` 删除文件，只删除清单内的文件；不要删除整个 Fcitx 目录。直接安装与包管理器安装应先卸载再切换，避免覆盖包管理器拥有的文件。

## 输入与配置

- libime 词典候选立即显示；模型完成后占据最前面的 n 个位置（通常 3）。仅去除文本重复项，保留“吗／嘛”等模型未覆盖的词典候选，候选不附加来源标记。
- 选择模型候选提交整句；选择词典候选后，本轮进入 libime 拆词模式，提交或清空后恢复模型候选。
- `=`／`+`／Tab 下一页，`-`／`_`／Shift+Tab 上一页；PageUp/PageDown 同样翻页，上下箭头移动候选。开始浏览后停止异步替换候选。
- 数字选词，空格确认；左右箭头、Home/End、Backspace/Delete 编辑拼音。光标在中间时使用词典候选；Enter 提交原始拼音，Escape 取消组合。
- 双拼支持自然码、微软、紫光、智能 ABC、中文之星、拼音加加、小鹤。按照 libime 的严格规则转成明确音节并精确查核心词表，关闭分词模型加载和近邻修复。
- 非法输入可直通，或在原位置显示删除线并在提交时去除。标点与全角字符使用 Fcitx 原生 punctuation/fullwidth 模块。
- 配置使用 Fcitx 原生系统，保存在 `conf/phono.conf`；核心参数热更新，非法配置或加载失败时回退到词典。模型候选不学习，词典学习保存在 Phono 独立目录。界面提供英文及简体、繁体 gettext 翻译。

上文来自应用报告的 surrounding text，取选区起点前的 Unicode 文本。插件处理重复、延迟和部分提交确认，并使用核心上下文管理器复用 KV。应用暂时不给有效文本时使用当前输入上下文的已知上文；失焦、reset 或转发应用编辑快捷键清除本地缓存，Escape 保留它。密码和敏感字段不推理、不记录上文、不学习。

LinuxQQ 等应用若不报告点击跳转后的 surrounding text，插件无法读取新位置的文档上文；当前接受这个应用侧限制。长期诊断使用 Fcitx 的 `phono-context` Debug 分类，仅记录前端、有效性、位置、长度与处理状态，不记录输入文本。可通过 `org.fcitx.Fcitx.Controller1.SetLogRule` 设置 `phono-context=5`，结束后恢复原日志规则。

## 开发与验证

需要 Linux、宿主 C++20 编译器、原生 Fcitx5 ≥ 5.1.7/libime ≥ 1.1.5 开发包、原生标点模块及 phono-core 共享库。Pixi 管理 CMake、Ninja、gettext 和通用头文件；addon 始终匹配宿主 Fcitx/libime ABI。

```sh
pixi install --locked
pixi run test
# 可选：用下载好的真实模型运行同一套集成测试
PHONO_TEST_MODEL_DIR=/absolute/path/to/model pixi run test
```

默认使用相邻 `phono-core/interface` 与 `phono-core/results/libphono_core.so`。其他安装位置通过 `pixi run python tools/configure.py -DPHONO_CORE_INCLUDE_DIR=... -DPHONO_CORE_LIBRARY=...` 设置；如使用匹配宿主的外部 SDK，可设置 `PHONO_SDK_PREFIX`。不再提供特定发行版快照的自动 SDK 下载或备用 vcpkg 构建路径。

保留七组测试：工作线程取消、候选/拆词/翻页、原生动态加载与上文事件、双拼转换、非法输入的 Unicode 显示和提交、用户词典 FD 读写、原生符号与双拼集成。模型测试复用动态加载和集成测试；gettext 目录由 `msgfmt --check --check-format` 在构建时验证。测试配置和历史位于隔离的构建目录。

[CI 流程与调用方法](docs/ci.md)说明二进制包构建、glibc 兼容性和下载位置。项目持续约束见 `AGENTS.md`。
