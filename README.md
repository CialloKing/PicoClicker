# PicoClicker

PicoClicker 是一个原生 Windows 鼠标连点器。x86 和 x64 各是一份可离线运行的 EXE。

## 使用

1. 输入点击间隔 `1–60,000 ms`，选择左键、中键或右键。
2. 点击「开始」或按全局 `F6`。启动后在当前光标位置连点，光标在 PicoClicker 窗口上时跳过该次点击。
3. 点击「停止」、再按一次 `F6`，或关闭窗口结束连点。

每次打开时默认左键和 100 ms。若窗口提示 F6 无法注册，释放该热键后重新打开 PicoClicker。设置值是请求的时间间隔，实际点击速度随系统调度和目标窗口而变化。

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
cmake --preset release-x64
cmake --build --preset release-x64
```

## 本地候选

两种构建完成后，在仓库根目录运行打包脚本。发布说明可以附上实测记录。

```powershell
.\scripts\pack.ps1 -VerificationReport '本地验收记录.md'
```

脚本从构建元数据读取版本号，核对架构、体积、导入表和 PE 保护配置，然后在 `dist/v<版本>` 写入两份 EXE、SHA-256 校验值和发布说明。体积门槛为 x86 8 KiB、x64 12 KiB。

验收时在专用接收窗口验证左、中、右键事件、后台 F6、热键冲突、停止及关闭，并记录 1、10、50 ms 档的实际 CPS 和间隔 P50、P99。

## 许可证

[GNU GPL v3](LICENSE)
