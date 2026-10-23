// TeXworksScript
// Title: 章节与定理导航
// Description: 在当前文件中定位章节、定理和引理，不修改源文件
// Author: Jieyang's Research Workspace
// Script-Type: standalone
// Context: TeXDocument

(function () {
  function maskComments(s) {
    return s.replace(/(^|[^\\])((?:\\\\)*)%[^\r\n]*/gm, function (m, p, slashes) {
      return p + slashes + new Array(m.length - p.length - slashes.length + 1).join(" ");
    });
  }
  var source = TW.target.text, clean = maskComments(source), entries = [], names = [];
  var commands = /\\(?:section|subsection|subsubsection|chapter|part)\*?\s*(?:\[[^\]]*\]\s*)?\{|\\begin\s*\{(?:theorem|thm|lemma|lem|proposition|prop|corollary|cor|definition|assumption|asm|remark|example)\}/g;
  var m;
  while ((m = commands.exec(clean)) !== null) {
    var title = m[0], pos = m.index, at = commands.lastIndex;
    if (m[0].indexOf("\\begin") === 0) {
      var env = /\{([^}]+)\}/.exec(m[0])[1];
      var extra = /^\s*\[([^\]]*)\]/.exec(clean.slice(at));
      var label = /^\s*(?:\[[^\]]*\]\s*)?\\label\s*\{([^}]*)\}/.exec(clean.slice(at));
      title = env + (extra ? " · " + extra[1] : "") + (label ? " · " + label[1] : "");
    } else {
      var depth = 1, end = at;
      for (; end < clean.length && depth; end++) {
        if (clean[end] === "\\") { end++; continue; }
        if (clean[end] === "{") depth++;
        if (clean[end] === "}") depth--;
      }
      title = (m[0].indexOf("sub") >= 0 ? "    " : "") + source.slice(at, end - 1).replace(/\s+/g, " ");
      commands.lastIndex = end;
    }
    var line = clean.slice(0, pos).split("\n").length;
    entries.push(pos);
    names.push("L" + line + "  " + title);
  }
  if (!entries.length) { TW.information(TW.target, "章节与定理导航", "当前文件没有可识别的章节或定理环境。"); return; }
  var initial = 0;
  for (var i = 0; i < entries.length; i++) if (entries[i] <= TW.target.cursorPosition) initial = i;
  var choice = TW.getItem(TW.target, "章节与定理导航", "当前文件 · 选择位置", names, initial, false);
  if (choice === null || choice === undefined) return;
  var index = names.indexOf(String(choice));
  if (index >= 0) TW.target.selectRange(entries[index], 0);
})();
