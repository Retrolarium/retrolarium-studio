#pragma once

#include "editors/editor_view.hpp"

#include <QPointer>

class QPushButton;
class QSplitter;

namespace rl::runtime {
class EmulatorRunner;
class EmulatorSurface;
class ProcessRunner;
}

namespace rl::sdk {
class EmulatorModel;
}

namespace rl::editors {

class EmulatorStage;

// The Playtest view: the running build at the screen's aspect and the session
// controls, with a side panel (the TAS editor) to the left of the picture when
// one is docked. MAME's log goes to the Messages panel.
class EmulatorView final : public EditorView {
    Q_OBJECT

public:
    // `processes` may be null; with it the view shows the build that leads
    // to a run.
    EmulatorView(sdk::EmulatorModel* model, runtime::EmulatorRunner* runner, runtime::ProcessRunner* processes,
                 QWidget* parent = nullptr);
    ~EmulatorView() override;

    QWidget* createToolbar(QWidget* parent) override;

    // Docks `panel` left of the picture (null takes the one there out). The
    // view does not own it: when the view goes, the panel goes back, hidden,
    // to the window.
    void setSidePanel(QWidget* panel);
    QWidget* sidePanel() const { return side_; }

private:
    void updateState();

    sdk::EmulatorModel* emulator_;
    runtime::EmulatorRunner* runner_;
    runtime::ProcessRunner* processes_;
    QString busyStep_;          // the kit step in progress, if any
    EmulatorStage* stage_;
    QSplitter* split_;
    QPointer<QWidget> side_;
};

}  // namespace rl::editors
