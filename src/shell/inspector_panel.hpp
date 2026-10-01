#pragma once

#include <QFrame>
#include <QPointer>

class QVBoxLayout;

namespace rl::sdk {
class EditorModel;
}

namespace rl::shell {

// The right-hand inspector, generated from the model's Inspection: the
// selection's fields, a library and the collapsed raw engine data.
class InspectorPanel final : public QFrame {
    Q_OBJECT

public:
    explicit InspectorPanel(QWidget* parent = nullptr);

    void setModel(sdk::EditorModel* model);
    // An inspector without a model (no project, kind without an editor).
    void showMessage(const QString& kicker, const QString& title, const QString& note);

signals:
    void fieldRejected(const QString& message);

private:
    void scheduleRebuild();
    void rebuild();
    void clearContent();

    QPointer<sdk::EditorModel> model_;
    QWidget* content_;
    QVBoxLayout* layout_;
    bool rebuildPending_ = false;
    bool engineExpanded_ = false;
};

}  // namespace rl::shell
