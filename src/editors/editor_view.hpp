#pragma once

#include <QWidget>

#include <memory>

namespace rl::sdk {
class EditorModel;
}

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

signals:
    void toolbarChanged();
    // A user action the model rejected, for the status bar.
    void actionFailed(const QString& message);

private:
    sdk::EditorModel* model_;
};

// The view for a model, or an empty-state view when no editor exists for its
// type yet.
std::unique_ptr<EditorView> createEditorView(sdk::EditorModel* model, QWidget* parent);

}  // namespace rl::editors
