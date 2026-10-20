#include "editors/editor_view.hpp"

#include "editors/composition_editor.hpp"
#include "editors/emulator_view.hpp"
#include "editors/hex_editor.hpp"
#include "editors/palette_editor.hpp"
#include "editors/sequencer_editor.hpp"
#include "editors/table_editor.hpp"
#include "editors/tile_editor.hpp"
#include "editors/tilemap_editor.hpp"
#include "sdk/editor_model.hpp"

namespace rl::editors {

EditorView::EditorView(sdk::EditorModel* model, QWidget* parent) : QWidget(parent), model_(model) {}

std::unique_ptr<EditorView> createEditorView(sdk::EditorModel* model, const EditorServices& services,
                                             QWidget* parent) {
    if (auto* map = qobject_cast<sdk::TileMapModel*>(model)) {
        return std::make_unique<TileMapEditor>(map, parent);
    }
    if (auto* tiles = qobject_cast<sdk::TileModel*>(model)) {
        return std::make_unique<TileEditor>(tiles, parent);
    }
    if (auto* palette = qobject_cast<sdk::PaletteModel*>(model)) {
        return std::make_unique<PaletteEditor>(palette, parent);
    }
    if (auto* bytes = qobject_cast<sdk::HexModel*>(model)) {
        return std::make_unique<HexEditor>(bytes, parent);
    }
    if (auto* table = qobject_cast<sdk::TableModel*>(model)) {
        return std::make_unique<TableEditor>(table, parent);
    }
    if (auto* composition = qobject_cast<sdk::CompositionModel*>(model)) {
        return std::make_unique<CompositionEditor>(composition, parent);
    }
    if (auto* sequencer = qobject_cast<sdk::SequencerModel*>(model)) {
        return std::make_unique<SequencerEditor>(sequencer, parent);
    }
    if (auto* emulator = qobject_cast<sdk::EmulatorModel*>(model)) {
        if (services.emulator) {
            return std::make_unique<EmulatorView>(emulator, services.emulator, services.processes, parent);
        }
    }
    return nullptr;
}

}  // namespace rl::editors
