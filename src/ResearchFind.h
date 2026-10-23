#pragma once
class TeXDocumentWindow;
namespace Research {
class FindController;
void showFind(TeXDocumentWindow *window,bool replace=false);
void findNext(TeXDocumentWindow *window,bool backwards=false);
void findSourceScopeChanged(TeXDocumentWindow *window);
}
