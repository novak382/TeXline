# TeXline 1.0 — maintenance update, 28 October 2026

This update keeps the application version at 1.0.0. The dated release identifies
the new packages without replacing the original release or its downloads.

## Beamer fixes

- Compiled slide previews keep their fitted size and reading position.
- The slide list follows supported template cover frames, included content and
  locally grouped closing slides. Editing an included file invalidates old mappings.
- Selecting, reordering, cutting or pasting slides, and undoing or redoing those
  actions, preserve the current frame view instead of switching it to full source.
- Starting a Beamer file from an empty document enters slide mode correctly.
- When a template cannot be mapped safely, the preview identifies the physical
  PDF page; unsupported structures remain protected from structural edits.

## Verification and limits

The same application binary passed 11 native hidden-desktop runs with 687 checks,
covering three interface languages, effective 100/125/150/200% scaling, narrow
windows, mapping, slide operations and undo/redo. Installed and rollback private
copies passed 105 self-checks each. Publication checks package integrity separately.
Hidden-desktop checks do not certify real IME input, physical mouse behavior,
mixed-DPI monitors or every custom Beamer macro. Existing demo recordings are
illustrations of the workflows, not recordings of this maintenance binary.

The source archive preserves all 1,335 recorded compiled source and test inputs
byte for byte. Public documentation is refreshed separately. Dependencies use
the unchanged base RebuildKit plus the required 1.0.0 SDK Update, linked from
the original release. A full cold toolchain rebuild is not certified.

Application SHA-256: `dda20dc020a1f79ed104686684bde4db8eab69dcf970c6328f0731ee187f0080`.

Windows 10/11 x64. Requires a local TeX Live or MiKTeX installation. The Windows
packages are unsigned. Save work and close TeXline before updating. The installer
preserves user settings and keeps the previous executable for rollback.
