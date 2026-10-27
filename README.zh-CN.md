<h1 align="center">TeXline</h1>

<p align="center">用 LaTeX 写作，留一些空间给思考。</p>

<p align="center">免费的本地编辑器，用于笔记、文章和 Beamer 讲稿。<br>Windows 10/11 x64<br>English · 简体中文 · 繁體中文</p>

<p align="center"><a href="https://texline.dev/#download"><strong>下载 Windows 版</strong></a> &nbsp; · &nbsp; <a href="https://texline.dev/">官网与演示</a> &nbsp; · &nbsp; <a href="README.md">English</a></p>

[![TeXline 中的考古笔记与编译后的页面，配有古希腊双耳陶罐图片。](docs/media/showcase/archaeology.png)](https://texline.dev/)

<p align="center"><sub>考古 · 从源码到页面。</sub></p>

TeXline 让正文成为界面的中心，文件仍是普通 LaTeX。使用本地 TeX 环境编译，通过 SyncTeX 往返源码与 PDF，无需注册账号。

[10 月 28 日维护更新](docs/releases/2026-10-28.md)：修复幻灯片预览大小、页面对应与编辑状态。

## 留下正文，接着写。

编辑全文，或只看当前章节。进入专注模式时，保留章节范围、选区与阅读位置。字体、行距可以自行调整，也可以开启打字机滚动。

<a href="https://texline.dev/#focus"><picture><source media="(prefers-reduced-motion: reduce)" srcset="docs/media/showcase/ecology.png"><img src="docs/media/showcase/ecology.gif" alt="生态学笔记从章节大纲进入专注模式，并在当前章节继续写作。"></picture></a>

<p align="center"><sub>生态 · 林冠下的光。</sub></p>

## 查阅时，思路不必离开。

把定理、证明或公式固定在草稿旁。查看它引用了什么、哪些位置引用它，连续查阅后回到原来的写作位置。引用关系来自显式 LaTeX 引用；源码与 PDF 支持双向 SyncTeX 定位。

[![谱界定理固定在证明下方，可在写作时随时对照。](docs/media/showcase/mathematics.png)](https://texline.dev/#writing)

<p align="center"><sub>数学 · 写证明时，定理就在眼前。</sub></p>

## 给讲稿更大的画面。

拖动调整 Beamer 幻灯片顺序，编辑当前页，逐步展示推导。展开预览后，仍能从左侧列表切换幻灯片。

<a href="https://texline.dev/#visual"><picture><source media="(prefers-reduced-motion: reduce)" srcset="docs/media/showcase/physics.png"><img src="docs/media/showcase/physics.gif" alt="将土星页拖到讲稿开头，逐步展示轨道推导，最后回到展开的土星预览。"></picture></a>

<p align="center"><sub>物理 · 从土星环到轨道运动。</sub></p>

[观看演示](https://texline.dev/) · [下载四组示例](https://texline.dev/samples/showcase-samples.zip) · [图片来源与录制说明](docs/media/README.md)

## 写作中需要的工具

- 可视化排列多图、编辑表格和矩阵、分段及对齐公式，预览后应用。支持的已有内容可再次打开编辑，应用后可撤销。
- 关联 `.bib` 文件，检索文献、插入引用，并修改已有引用的文献或页码。
- 改写前为选区保存命名版本，对比后取回需要的内容，恢复操作也可撤销。
- 保存写作现场，下次恢复章节、光标、选区、固定查阅和 PDF 位置。
- 编辑 `.tex`、`.bib`、`.sty` 和 `.cls` 文件，跨文件导航、复用模板、审核外部修改，使用内置 Git 流程。

菜单默认保留常用操作，可在设置中自定义。[查看各项功能与入口](docs/FEATURES.md)。

## 从一个文件开始

1. 安装 [TeX Live](https://tug.org/texlive/) 或 [MiKTeX](https://miktex.org/)，提供编译引擎与宏包。
2. 下载 [Windows 安装包](https://texline.dev/downloads/v1.0.0-20261028/TeXline-1.0.0-Setup-x64.exe) 或[便携 ZIP](https://texline.dev/downloads/v1.0.0-20261028/TeXline-1.0.0-Portable-x64.zip)，打开 `TeXline.exe`。
3. 打开文件，或从模板开始。选择“编译”即可查看 PDF。

安装包按当前用户安装，不改变文件关联，目前尚未签名。Git 功能需要另外安装 Git。

[安装说明](docs/INSTALL.md) · [第一次写作](docs/FIRST-SESSION.md) · [版本与校验值](https://github.com/novak382/TeXline/releases/tag/v1.0.0-20261028) · [已知限制](docs/KNOWN-LIMITATIONS.md)

## 反馈与参与

欢迎[提交问题](https://github.com/novak382/TeXline/issues)，说明想完成的操作，并附一个可复现的小例子。分享前请去除私人文本和个人路径。开发相关内容见[参与指南](CONTRIBUTING.md)与[构建说明](docs/BUILDING.md)。

## 基于 TeXworks

TeXline 是 [TeXworks](https://github.com/TeXworks/texworks) 的独立衍生项目。保留原作者声明及 GPL-2.0-or-later 授权；此 Qt 6 组合发行版采用 GPLv3 选项。TeXline 新增代码采用 GPL-3.0-or-later，素材和依赖各自遵循其许可。

[许可证](LICENSE) · [上游许可](COPYING) · [第三方声明](THIRD-PARTY-NOTICES.md) · [源码交付](docs/LICENSING.md)

<details>
<summary>重新构建 1.0</summary>

[发布附件](https://github.com/novak382/TeXline/releases/tag/v1.0.0-20261028)含应用源码、校验值、基础 RebuildKit 和包含已修改 Qt 源码的 1.0.0 SDK 更新包。重建时需要**两份 SDK 归档**，步骤与验证边界见[构建说明](docs/BUILDING.md)。

</details>
