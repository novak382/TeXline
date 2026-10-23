# Known limitations

- Windows x64 is the supported release target. Linux, macOS and Windows ARM
  inherited build paths are not certified by this release.
- The package is unsigned. Windows may show a reputation or consent prompt.
- Real IME composition, physical drag/drop, foreground animation appearance,
  taskbar behavior and mixed-DPI dual monitors still require human acceptance.
  Hidden-desktop and synthetic-input checks do not replace these trials.
- Very long full-source documents can pause during width changes or structural
  reflow. Section editing can reduce the visible workload. Internal timings are
  workload-specific and are not a universal latency bound.
- Structural recognition is conservative, not a TeX expansion engine. Unknown
  macros or ambiguous ranges can require ordinary source editing.
- A preview can remain stale until successful compilation. The editor retains
  the previous complete PDF or falls back to source when a fragment is uncertain.
- TeX and its packages are separate prerequisites. Keep independent backups.
- No external researcher trial or independent cold rebuild of the entire
  dependency toolchain is claimed. See [release evidence](RELEASE-V1.0.0.md).

Report issues at https://github.com/novak382/TeXline/issues without private
manuscripts, settings or personal paths.
