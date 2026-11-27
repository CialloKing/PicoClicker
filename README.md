# PicoClicker

PicoClicker 是一个原生 Windows 鼠标连点器，追求尽可能小的软件体积。普通版和极限精简版各有 x86、x64 两个构建，每个构建都是一份可离线运行的 EXE。高精度计时需要 Windows 10 1803 或更新版本。普通版和极限精简版启动时请求管理员权限，以便向同为管理员权限的窗口发送点击。

（本项目纯AI生成，绝无手写代码）

## 使用

1. 输入点击间隔 `1–60,000 ms`，选择左键、中键或右键。
2. 点击「开始」或按当前快捷键。启动后在当前光标位置连点，光标在 PicoClicker 窗口上时跳过该次点击。
3. 点击「停止」、再按一次当前快捷键，或关闭窗口结束连点。

默认设置为左键和 100 ms。实际点击速度随系统调度和目标窗口而变化。

- **普通版**首次运行使用 F1。点击「快捷键」一栏的按钮，按下单键或组合键，例如 `F8` 或 `Ctrl+Alt+K`。松开主键后应用，按 `Esc` 取消。改绑成功后将快捷键保存到 `%LOCALAPPDATA%\PicoClicker.dat`，下次启动时自动读取。配置缺失、无效或保存的组合被占用时，程序尝试使用 F1。
- **极限精简版**固定使用 F1 作为开始和停止的快捷键。F1 被占用时，可以用窗口按钮启停。
- **实验性极简版**启动时不主动请求提升权限。需要点击以管理员权限运行的窗口时，请先退出正在运行的实验版，再右键实验版 EXE，选择「以管理员身份运行」。[Microsoft SendInput 权限说明](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendinput)

## 构建

开发环境需要带 Windows SDK 的 Visual Studio C++ Build Tools、CMake 3.29 或更新版本和 LLVM。把 `clang-cl`、`lld-link` 和 `llvm-readobj` 加入 PATH。

在两个独立的 Visual Studio Developer Command Prompt 中，进入仓库后分别运行下列命令。

```bat
call "%VSINSTALLDIR%VC\Auxiliary\Build\vcvarsall.bat" amd64_x86
cmake --preset release-x86
cmake --build --preset release-x86
```

```bat
call "%VSINSTALLDIR%VC\Auxiliary\Build\vcvarsall.bat" amd64
cmake --fresh --preset release-x64
cmake --build --preset release-x64
```

每个预设都在对应的 `build/release-*` 目录生成普通版 `PicoClicker.exe` 和极限精简版 `PicoClicker-Lite.exe`。单独构建时，可在构建命令末尾加 `--target PicoClicker` 或 `--target PicoClickerLite`。

使用 clang-cl 和 LLD 时，可在单独的构建目录中对普通版和 Lite 启用加载器 Cookie，生成标准 PE 布局的候选。x86 构建使用对应的预设和目录名。

```bat
cmake --preset release-x64 -B build/loader-cookie-x64 -DPICOCLICKER_STANDARD_LOADER_COOKIE=ON
cmake --build build/loader-cookie-x64
```

### 实验性极简版

先运行上面对应架构的 CMake 配置命令，再显式构建 `PicoClickerExperimental`。该目标需要 clang-cl、LLD 和 PowerShell，并在对应构建目录生成 `PicoClicker-Experimental.exe`。

```bat
cmake --build --preset release-x86 --target PicoClickerExperimental
cmake --build --preset release-x64 --target PicoClickerExperimental
```

实验版沿用 Lite 的固定 F1 快捷键，提供极简英文界面。运行时文本使用 ASCII，窗口和控件消息使用 ANSI Win32 API。

连点时可以编辑间隔和切换鼠标按键，改动在下次启动连点时生效。主窗口省去了 Tab 切换焦点、方向键切换单选项、Enter 激活默认按钮和 Esc 关闭窗口的对话框快捷键。可直接键入数字，用鼠标选择按键，用 F1 或界面按钮启停，点击标题栏关闭按钮退出。[Microsoft 对话框键盘行为](https://learn.microsoft.com/en-us/windows/win32/dlgbox/dlgbox-programming-considerations#dialog-box-keyboard-interface)

两种架构都从 `clicker.rc` 生成对话框模板，启动时把紧凑编码还原为原始模板，再交由 `CreateDialogIndirectParamA` 创建窗口。实验版成品省去 PE 资源目录。[Microsoft 对话框 API](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-createdialogindirectparama)

实验版由 Windows 加载器初始化栈保护 Cookie，用同一张表查找和接收导入地址。两种架构都用手写汇编处理窗口回调、等待和点击，x64 带有异常展开信息。打包会裁掉磁盘上最后一个节的填充，并可将部分导入名称和重定位表放入 PE 文件头或代码节的填充区。部分 Windows 版本或 PE 工具可能拒绝该布局。使用 LLVM 22.1.8 的 `llvm-readobj` 检查导入表和重定位时，可读取 `PicoClicker-Experimental-linked.exe`。[Microsoft PE 格式规范](https://learn.microsoft.com/en-us/windows/win32/debug/pe-format)

| 架构 | 打包后的 Lite | 实验性极简版 |
| --- | ---: | ---: |
| x86 | 4,096 字节 | 2,042 字节 |
| x64 | 5,120 字节 | 2,441 字节 |

两种架构均在 Windows 10 22H2 上直接使用成品验证了普通权限启动、实际点击、运行中编辑、F1 启停、导入地址、加载器写入的 Cookie 和节区权限。[本轮体积分析](docs/size-analysis-v0.4.5.md#v0416-的-x64-优化)

## 本地候选

两种架构构建完成后，在仓库根目录运行打包脚本。发布说明可以附上实测记录。

```powershell
.\scripts\pack.ps1 -VerificationReport '本地验收记录.md'
```

打包时，若只读数据节有足够空间，脚本会将重定位表原样搬入其中，并按实际节数缩短文件头。极限精简版和普通版 x64 在链接时为资源树预留空间，打包后窗口和清单仍位于 PE 的 `.rsrc` 节首。修改窗口资源或清单后，应重新核对预留大小和成品体积。

两个 x64 版本在使用 clang-cl 和 LLD 时都采用紧凑 PE 加载配置。打包脚本会分别检查两个版本的保护标志和链接映射。各项优化的实测收益见[反汇编体积分析](docs/size-analysis-v0.4.5.md)。

脚本核对成品的架构、体积、导入表、资源和 PE 保护配置，然后在 `dist/v<版本>` 写入四份 EXE、SHA-256 校验值和发布说明。比对体积时以 `dist` 中的成品为准。

构建两种架构的实验版后，可在打包命令末尾加 `-IncludeExperimental`，将它们一并纳入发布说明和校验清单。

普通版的体积上限为 x86 9 KiB、x64 13 KiB，极限精简版的上限为 x86 6 KiB、x64 7.5 KiB。

验收时在专用接收窗口验证左、中、右键事件、停止和关闭，并记录 1、10、50 ms 档的实际 CPS 和间隔 P50、P99。普通版额外验证改绑后的后台启停、重启恢复和热键冲突，极限精简版额外验证 F1 和快捷键冲突时的按钮启停。

## 许可证

[GNU GPL v3](LICENSE)
