# PicoClicker

PicoClicker 是一个原生 Windows 鼠标连点器。普通版和极限精简版各有 x86、x64 两个构建，每个构建都是一份可离线运行的 EXE。高精度计时需要 Windows 10 1803 或更新版本。启动时请求管理员权限，以便向同为管理员权限的窗口发送点击。

## 使用

1. 输入点击间隔 `1–60,000 ms`，选择左键、中键或右键。
2. 点击「开始」或按当前快捷键。启动后在当前光标位置连点，光标在 PicoClicker 窗口上时跳过该次点击。
3. 点击「停止」、再按一次当前快捷键，或关闭窗口结束连点。

默认设置为左键和 100 ms。实际点击速度随系统调度和目标窗口而变化。

- **普通版**首次运行使用 F1。点击「快捷键」一栏的按钮，按下单键或组合键，例如 `F8` 或 `Ctrl+Alt+K`。松开主键后应用，按 `Esc` 取消。改绑成功后将快捷键保存到 `%LOCALAPPDATA%\PicoClicker.dat`，下次启动时自动读取。配置缺失、无效或保存的组合被占用时，程序尝试使用 F1。
- **极限精简版**固定使用 F1 启停。F1 被占用时，可以用窗口按钮启停。

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

## 本地候选

两种架构构建完成后，在仓库根目录运行打包脚本。发布说明可以附上实测记录。

```powershell
.\scripts\pack.ps1 -VerificationReport '本地验收记录.md'
```

打包时，若只读数据节有足够空间，脚本会将重定位表原样搬入其中，并按实际节数缩短文件头。极限精简版 x86 和普通版 x64 在链接时为资源树预留空间，打包后窗口和清单仍位于 PE 的 `.rsrc` 节首。修改窗口资源或清单后，应重新核对预留大小和成品体积。

脚本核对成品的架构、体积、导入表、资源和 PE 保护配置，然后在 `dist/v<版本>` 写入四份 EXE、SHA-256 校验值和发布说明。比对体积时以 `dist` 中的成品为准。

普通版的体积上限为 x86 9 KiB、x64 13 KiB，极限精简版的上限为 x86 6 KiB、x64 7.5 KiB。

验收时在专用接收窗口验证左、中、右键事件、停止和关闭，并记录 1、10、50 ms 档的实际 CPS 和间隔 P50、P99。普通版额外验证改绑后的后台启停、重启恢复和热键冲突，极限精简版额外验证 F1 和快捷键冲突时的按钮启停。

## 许可证

[GNU GPL v3](LICENSE)
