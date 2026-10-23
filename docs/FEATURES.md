# A writing workflow, from the first line to the talk

[Download TeXline](https://github.com/novak382/TeXline/releases/tag/v1.0.0) · [Back to the overview](../README.md)

These are existing TeXline 1.0 workflows. Menus adapt to the document and selection; command search remains available when an optional menu entry is hidden.

## Begin with a file

Open your existing `.tex` file or use **New file** on the start page. A document, a Beamer talk and blank source are different starting points for the same ordinary files. Recently opened files are nearby, and the start actions disappear as you write.

For a multi-file project, keep its folders together. TeXline recognizes supported main-file and engine hints; check the selected engine before the first compilation. A separately installed TeX distribution supplies the compiler and packages. The PDF opens through the **PDF** entry, with SyncTeX available for source/output navigation.

## Write at the right scale

The outline is useful when moving through a long document. **Full source / Section** controls how much of it you edit at once. Focus mode preserves that scope rather than forcing you back to the whole file.

**Settings → Writing** controls font, size and spacing with a live preview; cancelling restores the prior appearance. Typewriter mode is optional. It keeps the active line at a comfortable height during typing while allowing manual selection, scrolling and reading.

## Keep the evidence beside the argument

Use the outline and reading tools to pin a recognized theorem, proof or equation. Pinned reading stays available as you work elsewhere. The context action for the current object opens its content, nearby proof and explicit references in both directions. Follow a source link, then use navigation back to return through your reading trail.

These are relationships written in labels, references and citation commands. TeXline does not claim to infer the mathematical premises of a proof.

For literature, use **Insert → Citation…**. Link a `.bib` file if none is associated, search its entries, select the references and insert them. Open a recognized existing citation to revise the selection, page detail or citation form in place. The bibliography remains an editable file in the project.

## Keep alternatives and return later

Select a passage and choose **Keep selection version…** from the relevant context menu or command search. Give it a useful name, such as “geometric proof”, before trying another argument. The version window compares content and also offers exact source comparison. Recover a selected part or restore the passage; restoration participates in undo.

Use **Save writing place…** when stopping at a meaningful point. A named place includes the section range, cursor and selection, pinned reading, PDF reading position and relevant layout. Reopen the project and restore that place to continue. If the source has changed substantially, inspect the restored location rather than assuming an old position is still meaningful.

Local passage versions help with a rewrite. Project Git history helps with changes across files. They serve different scales of work.

## Edit the object you are thinking about

Open a recognized figure group, table or formula structure through its contextual action. The editor creates a draft: change the arrangement, inspect the preview and apply it when ready. Cancelling leaves the document alone; applying supported changes can be undone.

- **Figures:** use individual and group captions, columns and consistent sizing. Clipboard image import asks for confirmation before resources are added.
- **Tables:** work on cell ranges and paste tab-separated spreadsheet data; clear cells without rebuilding the table by hand.
- **Formula structures:** edit supported matrices, cases and aligned expressions with the existing LaTeX engine rendering the preview.

Recognition is deliberately conservative. Custom macro systems and complex nesting may need the source editor. A typeset preview needs a working TeX installation and the relevant packages.

## Turn a document into a conversation

Beamer mode presents the slide list, current frame source and compiled preview together. Reorder frames, duplicate or paste them, and undo a change. Expand the preview to temporarily hide the source while retaining slide navigation; return to editing with the same control.

The progressive reveal editor lets you assign supported items to steps and inspect the resulting pages. Speaker notes and projection/speaker export use the existing Beamer workflow.

Templates can be reused through the template library or kept in the project folder. Third-party templates retain their own licenses and package requirements. No particular university template is bundled or endorsed by TeXline.

## Keep ownership of the project

The editor works with source, bibliography, class and style files. Project operations, external-change review and Git are available without making the writing surface a dashboard. Git supports repository-based collaboration; TeXline does not provide simultaneous co-editing.

Local folders synchronized by Dropbox, OneDrive or Google Drive can use local compilation. Their clients handle synchronization, and files must actually be available locally. A cloud account is not needed for a local project.

## Make the workspace yours

The interface is available in English, 简体中文 and 繁體中文. Settings group writing, PDF reading, menus and shortcuts, compilation and advanced controls. Optional menu entries can be enabled without changing the availability of their commands or shortcuts.

[Install](INSTALL.md) · [Try the sample](../examples/showcase) · [Known limitations](KNOWN-LIMITATIONS.md) · [Report an issue](https://github.com/novak382/TeXline/issues)
