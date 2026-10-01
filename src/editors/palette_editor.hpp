#pragma once

#include "editors/editor_view.hpp"

class QVBoxLayout;
class QGridLayout;

namespace rl::sdk {
class PaletteModel;
}

namespace rl::editors {

// Universal palette editor: swatch rows, a picker for the selected entry
// (fixed gamut or per-channel levels) and "Where it's used" samples.
class PaletteEditor final : public EditorView {
    Q_OBJECT

public:
    explicit PaletteEditor(sdk::PaletteModel* model, QWidget* parent = nullptr);

    QWidget* createToolbar(QWidget* parent) override;

private:
    void rebuild();
    void scheduleRebuild();

    sdk::PaletteModel* palette_;
    QWidget* content_;
    QVBoxLayout* left_;
    QVBoxLayout* right_;
    bool pending_ = false;
};

}  // namespace rl::editors
