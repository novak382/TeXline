// TeXworksScript
// Title: 跳转到定义
// Description: 跳到当前文件的 label；选中引用键时直接定位
// Author: Jieyang's Research Workspace
// Script-Type: standalone
// Context: TeXDocument
// Shortcut: Ctrl+Shift+L

(function () {
  var text = TW.target.text;
  var clean = text.replace(/(^|[^\\])((?:\\\\)*)%[^\r\n]*/gm, function (m, p, s) {
    return p + s + new Array(m.length - p.length - s.length + 1).join(" ");
  });
  var re = /\\label\s*\{([^}]*)\}/g, m, entries = [], names = [];
  var wanted = String(TW.target.selection || "").replace(/^\s+|\s+$/g, "");
  var refs = /\\(?:ref|eqref|autoref|cref|Cref)\*?\s*\{([^}]*)\}/g, ref;
  if (!wanted) while ((ref = refs.exec(clean)) !== null) {
    if (ref.index <= TW.target.cursorPosition && refs.lastIndex >= TW.target.cursorPosition) { wanted = ref[1]; break; }
  }
  while ((m = re.exec(clean)) !== null) {
    entries.push({ key: m[1], pos: m.index });
    names.push(m[1] + "  ·  L" + clean.slice(0, m.index).split("\n").length);
  }
  for (var i = 0; i < entries.length; i++) if (wanted && entries[i].key === wanted) {
    TW.target.selectRange(entries[i].pos, 0); return;
  }
  if (!entries.length) { TW.information(TW.target, "跳转到定义", "当前文件没有 label。此功能只搜索当前文件。"); return; }
  var choice = TW.getItem(TW.target, "跳转到定义", wanted ? "当前文件未找到 " + wanted + "；选择其他标签" : "当前文件 · 选择标签", names, 0, false);
  if (choice === null || choice === undefined) return;
  var index = names.indexOf(String(choice));
  if (index >= 0) TW.target.selectRange(entries[index].pos, 0);
})();
