<p align="center"><img src="docs/media/wordmark.svg" width="560" alt="TeXline — 更安静的 LaTeX 写作空间"></p>

<p align="center">写一份笔记，理清一段证明，准备一场报告。<br>把注意力留给内容，文件仍是熟悉的 LaTeX。</p>

<p align="center"><strong><a href="https://github.com/novak382/TeXline/releases/download/v1.0.0/TeXline-1.0.0-Setup-x64.exe">下载 Windows 版</a></strong> · <a href="https://github.com/novak382/TeXline/releases/download/v1.0.0/TeXline-1.0.0-Portable-x64.zip">便携版</a> · <a href="README.md">English</a></p>

![本节正文下方固定显示定理的实际排版结果。](docs/media/reading.png)

<p align="center"><sub>TeXline 1.0 实机画面 · 原生 2× 截图 · <a href="docs/media/reading.png">查看原图</a></sub></p>

TeXline 是面向文件、论文与 Beamer 讲稿的 Windows LaTeX 编辑器。浅色、克制的写作界面，配合按需展开的查阅和可视化编辑工具。支持 English、简体中文和繁體中文。

## 一次专注于一段内容

从目录进入章节，在全文与本节之间切换。进入专注模式时，保留当前章节、选区和阅读位置。字体与行距调整立即预览；打字机模式和更多菜单入口可以按需开启。

<details>
<summary>查看源码与 PDF 并排画面</summary>

![两个完整窗口展示源码与居中的整页 PDF。](docs/media/writing.png)

</details>

## 查阅之后，回到思路所在

把定理、证明或公式固定在正文旁，继续写作。打开此处上下文，沿显式引用查看“这里引用了什么”和“哪些地方引用这里”，连续跳转后返回。

关联本地 `.bib` 文件，搜索并插入引用；已有引用也可重新打开，修改文献选择、页码或引用方式。查阅展示的是源码中的引用关系，不推断数学逻辑依赖。

## 敢于改写，也方便接着写

改写前，给选区保留一个命名版本。比较内容变化，必要时查看准确源码，再取回局部内容或恢复整段；恢复操作可以撤销。

停笔时保存写作现场，下次恢复章节范围、光标与选区、固定查阅、PDF 位置及相关布局。

## 排版需要直觉时，就用可视化工具

![多图排版的真实预览与布局控件。](docs/media/figures.png)

排列多张图片，调整列数与尺寸，编辑子图和总标题；编辑矩阵、分段和对齐公式的结构；复制粘贴表格单元格区域；把剪贴板图片确认导入项目。

识别到的已有对象可以重新编辑。预览后再应用，取消保留原文。复杂宏和不明确的结构仍可直接编辑源码。

## 让讲稿占据应有的空间

![实际程序中的幻灯片导航与展开预览。](docs/media/slides.png)

打开 Beamer 文件，逐张编辑、调整顺序、添加备注和设置逐步显示。展开预览后，暂时隐藏源码，保留左侧幻灯片列表；需要修改时再回到编辑。

<details>
<summary>播放幻灯片导航与展开演示</summary>

![实际程序中的幻灯片导航与展开预览录制。](docs/media/slides.gif)

</details>

## 文件始终在自己的目录里

支持多文件项目、主文件与引擎识别、外部修改审核和内置 Git 流程，也可编辑 `.bib`、`.sty`、`.cls` 等文件。模板既可以导入复用，也可以直接保存在项目文件夹。

本地编辑与编译不需要 TeXline 账号或云服务。Dropbox、OneDrive、Google Drive 的本地可用文件夹可以沿用本地工作方式；同步由对应客户端负责。

[查看完整功能与入口（English）](docs/FEATURES.md)

## 开始使用

1. 单独安装 [TeX Live](https://tug.org/texlive/) 或 [MiKTeX](https://miktex.org/)。
2. 下载安装包，或把便携版解压到可写文件夹，打开其中的 `TeXline.exe`。
3. 打开文件，或在空白页新建文档，点击编译查看 PDF。

需要 Windows 10/11 x64。安装包按当前用户安装，不新增快捷方式、不接管文件关联；当前程序尚未签名。[安装说明](docs/INSTALL.md) · [已知限制](docs/KNOWN-LIMITATIONS.md) · [展示样稿](examples/showcase)

## 开源与反馈

TeXline 是 TeXworks 的独立衍生项目，保留上游版权与许可声明。组合 Qt 6 发行版采用 GPLv3；各项资源与依赖遵守各自许可。发布页提供源码、校验和及依赖重建资料。

[反馈问题](https://github.com/novak382/TeXline/issues) · [参与贡献](CONTRIBUTING.md) · [许可证](LICENSE) · [第三方声明](THIRD-PARTY-NOTICES.md) · [源码构建](docs/BUILDING.md) · [图片来源](docs/media/README.md)
