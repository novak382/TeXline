// TeXworksScript
// Title: 选取当前证明或环境
// Description: 优先选取光标所在 proof；否则选取包含光标的最内层环境
// Author: Jieyang's Research Workspace
// Script-Type: standalone
// Context: TeXDocument
// Shortcut: Ctrl+Shift+P

(function () {
  var text = TW.target.text;
  var clean = text.replace(/(^|[^\\])((?:\\\\)*)%[^\r\n]*/gm, function (m, p, s) {
    return p + s + new Array(m.length - p.length - s.length + 1).join(" ");
  });
  var re = /\\(begin|end)\s*\{([^}]+)\}/g, stack = [], ranges = [], m;
  var pos = TW.target.cursorPosition;
  while ((m = re.exec(clean)) !== null) {
    if (m[1] === "begin") { stack.push({ env: m[2], start: m.index }); }
    else if (stack.length && stack[stack.length - 1].env === m[2]) {
      var opening = stack.pop();
      if (opening.env !== "document" && opening.start <= pos && re.lastIndex >= pos) {
        ranges.push({ env: opening.env, start: opening.start, end: re.lastIndex });
      }
    }
  }
  var range = null;
  for (var i = 0; i < ranges.length; i++) if (ranges[i].env === "proof" || ranges[i].env === "proof*") { range = ranges[i]; break; }
  if (!range && ranges.length) range = ranges[0];
  if (!range) { TW.information(TW.target, "选取当前证明或环境", "光标不在可识别的 begin/end 环境内。自定义证明宏请手动选取。"); return; }
  TW.target.selectRange(range.start, range.end - range.start);
})();
