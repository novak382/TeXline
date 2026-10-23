# TeXline

[English](README.md) · [下载 1.0](https://github.com/novak382/TeXline/releases/tag/v1.0.0) · [首次写作](docs/FIRST-SESSION.md) · [构建](docs/BUILDING.md)

基于 [TeXworks](https://github.com/TeXworks/texworks) 的 Windows x64 LaTeX 编辑器。
沿用浅色、克制的界面，直接编辑 `.tex` 文件，内置 PDF 阅读与双向 SyncTeX。

- **写作：**多文件目录、本节编辑、固定查阅和专注模式。
- **研究：**本地文献搜索与引用修改、显式引用关系查阅、保存写作现场与局部版本。
- **讲稿：**Beamer 导航、排序、备注、展开预览，以及多图、表格、公式结构和逐步显示的可视化编辑。
- **项目：**审核外部修改、内置 Git，保留普通文件夹的工作方式。

支持简体中文、繁體中文和 English。编辑在本地进行，不要求 AI 服务或云账户。
编译需另行安装 TeX Live、MiKTeX 等 TeX 发行版。

## 开始使用

从[发布页](https://github.com/novak382/TeXline/releases/tag/v1.0.0)下载
`TeXline-1.0.0-Setup-x64.exe`，或将 `TeXline-1.0.0-Portable-x64.zip` 解压到可写目录。
安装包按当前用户安装，不新增快捷方式，也不修改默认文件关联；从安装目录打开 `TeXline.exe`。
参阅[安装说明](docs/INSTALL.md)，用随包示例完成[首次写作](docs/FIRST-SESSION.md)。

安装包暂未签名。真实输入法、前台动画观感和双屏仍需用户验收；
详情见[已知限制](docs/KNOWN-LIMITATIONS.md)。

## 反馈与贡献

欢迎在 [Issues](https://github.com/novak382/TeXline/issues) 提交问题。
附上版本、系统缩放和可分享的最小示例，去除个人路径及私密内容。
参阅[反馈模板](docs/FEEDBACK.md)及[贡献说明](CONTRIBUTING.md)。

## 许可与源码

TeXline 是独立的 TeXworks 衍生项目，保留上游版权和 GPL-2.0-or-later 源码授权。
Qt 6 组合发行选择 GPLv3，TeXline 新增代码采用 GPL-3.0-or-later；独立素材与依赖保留各自许可。
参阅 [LICENSE](LICENSE)、[COPYING](COPYING)、[第三方声明](THIRD-PARTY-NOTICES.md)
及[源码交付说明](docs/LICENSING.md)。

发布页同时提供源码、基础 RebuildKit 和包含 Qt 修改源码的 1.0.0 SDK 更新包。
构建本版时须同时使用两份 SDK 归档；步骤与验证边界见[构建说明](docs/BUILDING.md)。
