// UndoStack.cpp
#include "UndoStack.h"
#include "SceneGlobals.h"

UndoStack& UndoStack::Instance() { static UndoStack i; return i; }

EditorSnapshot UndoStack::CaptureCurrent() const {
    EditorSnapshot s;
    s.brushes        = g_brushes;
    s.entities       = g_entities;
    s.selectedBrush  = g_iSelectedBrush;
    s.selectedEntity = g_iSelectedEntity;
    return s;
}

void UndoStack::ApplySnapshot(const EditorSnapshot& s) {
    g_brushes        = s.brushes;
    g_entities       = s.entities;
    g_iSelectedBrush  = s.selectedBrush;
    g_iSelectedEntity = s.selectedEntity;
}

void UndoStack::PushSnapshot(const char* label) {
    EditorSnapshot s = CaptureCurrent();
    s.label = label ? label : "";
    m_undoStack.push_back(s);
    if (m_undoStack.size() > MAX_DEPTH) m_undoStack.erase(m_undoStack.begin());
    m_redoStack.clear();
}

bool UndoStack::Undo() {
    if (m_undoStack.empty()) return false;
    EditorSnapshot cur = CaptureCurrent();
    cur.label = m_undoStack.back().label;
    m_redoStack.push_back(cur);
    EditorSnapshot s = m_undoStack.back();
    m_undoStack.pop_back();
    ApplySnapshot(s);
    return true;
}

bool UndoStack::Redo() {
    if (m_redoStack.empty()) return false;
    EditorSnapshot cur = CaptureCurrent();
    cur.label = m_redoStack.back().label;
    m_undoStack.push_back(cur);
    EditorSnapshot s = m_redoStack.back();
    m_redoStack.pop_back();
    ApplySnapshot(s);
    return true;
}

std::string UndoStack::GetUndoLabel() const {
    return m_undoStack.empty() ? "" : m_undoStack.back().label;
}
std::string UndoStack::GetRedoLabel() const {
    return m_redoStack.empty() ? "" : m_redoStack.back().label;
}
void UndoStack::Clear() { m_undoStack.clear(); m_redoStack.clear(); }