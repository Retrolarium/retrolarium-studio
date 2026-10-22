#pragma once

#include <QWidget>

#include <memory>

namespace rl::sdk {
class EditorModel;
}

namespace rl::runtime {
class EmulatorRunner;
class ProcessRunner;
}  // namespace rl::runtime

namespace rl::editors {

// Base of the universal editors. The shell hosts the view in the centre and
// its toolbar in the editor toolbar row; the view talks only to its model.
class EditorView : public QWidget {
    Q_OBJECT

public:
    explicit EditorView(sdk::EditorModel* model, QWidget* parent = nullptr);

    sdk::EditorModel* model() const { return model_; }

    // Contents of the 38px editor toolbar row: tools, layers, view chips.
    // Called again after toolbarChanged().
    virtual QWidget* createToolbar(QWidget* parent) = 0;
    // Picks a library item and arms the tool that places it; false where
    // this editor places nothing.
    virtual bool armPlacement(const QString& item) {
        Q_UNUSED(item);
        return false;
    }

signals:
    void toolbarChanged();
    // A user action the model rejected, for the status bar.
    void actionFailed(const QString& message);

private:
    sdk::EditorModel* model_;
};

// Studio services some editors need beyond their model.
struct EditorServices {
    runtime::EmulatorRunner* emulator = nullptr;
    runtime::ProcessRunner* processes = nullptr;
};

// The universal editor for a model, or nullptr when none exists for its type.
std::unique_ptr<EditorView> createEditorView(sdk::EditorModel* model, const EditorServices& services, QWidget* parent);

}  // namespace rl::editors
