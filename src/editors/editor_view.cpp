#include "editors/editor_view.hpp"

#include "editors/palette_editor.hpp"
#include "editors/table_editor.hpp"
#include "editors/tile_editor.hpp"
#include "editors/tilemap_editor.hpp"
#include "sdk/editor_model.hpp"

namespace rl::editors {

EditorView::EditorView(sdk::EditorModel* model, QWidget* parent) : QWidget(parent), model_(model) {}

std::unique_ptr<EditorView> createEditorView(sdk::EditorModel* model, QWidget* parent) {
    if (auto* map = qobject_cast<sdk::TileMapModel*>(model)) {
        return std::make_unique<TileMapEditor>(map, parent);
    }
    if (auto* tiles = qobject_cast<sdk::TileModel*>(model)) {
        return std::make_unique<TileEditor>(tiles, parent);
    }
    if (auto* palette = qobject_cast<sdk::PaletteModel*>(model)) {
        return std::make_unique<PaletteEditor>(palette, parent);
    }
    if (auto* table = qobject_cast<sdk::TableModel*>(model)) {
        return std::make_unique<TableEditor>(table, parent);
    }
    return nullptr;
}

}  // namespace rl::editors
