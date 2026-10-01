#pragma once

#include "editors/editor_view.hpp"

class QPlainTextEdit;
class QPushButton;

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

// The Playtest view: the running build at the screen's aspect, its log and
// the session controls.
class EmulatorView final : public EditorView {
    Q_OBJECT

public:
    // `processes` may be null; with it the view shows the build that leads
    // to a run.
    EmulatorView(sdk::EmulatorModel* model, runtime::EmulatorRunner* runner, runtime::ProcessRunner* processes,
                 QWidget* parent = nullptr);
    ~EmulatorView() override;

    QWidget* createToolbar(QWidget* parent) override;

private:
    void updateState();

    sdk::EmulatorModel* emulator_;
    runtime::EmulatorRunner* runner_;
    runtime::ProcessRunner* processes_;
    QString busyStep_;          // the kit step in progress, if any
    EmulatorStage* stage_;
    QPlainTextEdit* log_;
};

}  // namespace rl::editors
