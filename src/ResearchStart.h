// Contextual starting points for a new, empty writing window. GPL-2.0-or-later.
#pragma once
class TeXDocumentWindow;
namespace Research {
// Install after the window's existing Open / New from Template actions are wired.
void installWritingStart(TeXDocumentWindow *window);
// Apply saved visibility preferences without changing the editor or its history.
void refreshWritingStart(TeXDocumentWindow *window);
}
