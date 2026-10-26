// UndoStack.h
#ifndef APP_UNDO_STACK_H
#define APP_UNDO_STACK_H

#include <string>
#include <vector>
#include "Brush.h"
#include "Entity.h"

struct EditorSnapshot {
    std::vector<CBrush>  brushes;
    std::vector<CEntity> entities;
    int selectedBrush  = -1;
    int selectedEntity = -1;
    std::string label;
};

class UndoStack {
public:
    static UndoStack& Instance();

    void PushSnapshot(const char* label);
    bool Undo();
    bool Redo();

    bool CanUndo() const { return !m_undoStack.empty(); }
    bool CanRedo() const { return !m_redoStack.empty(); }

    std::string GetUndoLabel() const;
    std::string GetRedoLabel() const;
    void Clear();

private:
    EditorSnapshot CaptureCurrent() const;
    void ApplySnapshot(const EditorSnapshot& snap);

    static const size_t MAX_DEPTH = 64;
    std::vector<EditorSnapshot> m_undoStack;
    std::vector<EditorSnapshot> m_redoStack;
};

#endif