# Install / 安装

## Requirements / 环境

- Windows 10/11 x64; this preview has not certified Windows on ARM
- A writable per-user installation directory
- TeX Live or MiKTeX installed separately, with the chosen engine and packages

The package contains the editor, PDF support data, fonts, local introduction,
examples and notices. It does not bundle TeX, an AI service or an updater.
The unsigned preview may trigger Windows reputation warnings. Check the
supplied SHA256SUMS and your distribution channel before deciding to run it;
do not disable system protection globally.

## Setup

Run `TeXline-1.0.0-Setup-x64.exe`. The default is
`%LOCALAPPDATA%\Programs\TeXline`; no administrator rights are required.
It creates no shortcuts and does not take over `.tex` or `.pdf` associations.
Start `TeXline.exe` in that folder. Choose 简体中文, 繁體中文 or English in the
introduction or settings. Close your own TeXline session before upgrading; the
installer refuses a locked executable instead of forcing it closed.

Settings, recovery information and local resources live beside the application
under `settings` and `resources`. These directories remain after uninstall;
back them up before transferring or removing the folder. Setup never ships
another user's settings. An upgrade keeps them and takes a local copy of the
previous executable under `rollback` before replacement. A rollback executable
alone is not a full rollback of resources: keep the prior portable package when
you need to revert a whole preview. Your manuscripts are never installation
files and should be stored in your own writing folder.

## First compilation

Copy `examples/first-session` from the installation into your own writable
writing folder. Open `paper/main.tex`, select pdfLaTeX and compile. If no engine
is found, restart the editor after installing TeX, or add the TeX binary folder
in its tool settings. Missing packages must be installed by your TeX distribution.
Use [FIRST-SESSION](FIRST-SESSION.md) to test navigation, a real edit, compilation,
Beamer and undo. Keep the complete folder when sharing a minimal reproducer.

## Portable and uninstall

The portable ZIP uses the same payload; extract into a writable folder and run
`TeXline.exe`. A removable drive must allow writes. For a Setup installation,
use Windows Installed apps or `unins000.exe`. Only installed payload files are
removed; user-created files, settings and recovery data are preserved. Manually
remove those only after verifying that they contain nothing you need.

## 中文要点

需要单独安装 TeX Live 或 MiKTeX。Setup 默认安装至当前用户的可写目录，不抢占文件
关联、不创建快捷方式。升级前请自行关闭 TeXline；文件被占用时安装会停止，不强行
关闭进程。设置和恢复资料位于程序旁的 `settings`，卸载保留这些资料及用户创建的
文件。首次写作先把随包示例复制到自己的写作目录，再打开、改稿和编译。
