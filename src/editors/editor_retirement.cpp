#include "editors/editor_retirement.hpp"

#include "editors/editor_view.hpp"
#include "editors/emulator_view.hpp"
#include "runtime/emulator_runner.hpp"
#include "sdk/editor_model.hpp"
#include "sdk/session.hpp"

namespace rl::editors {

struct EditorRetirement::Editor {
    // Reverse destruction order: the view goes before the model it reads.
    std::unique_ptr<sdk::EditorModel> model;
    std::unique_ptr<EditorView> view;
};

EditorRetirement::EditorRetirement(runtime::EmulatorRunner* runner) : runner_(runner) {
    // Connect before any views: retired views disappear before a replacement
    // attaches its surface, and before the runner starts a pending launch.
    connect(runner_, &runtime::EmulatorRunner::stopped, this, &EditorRetirement::clear);
}

EditorRetirement::~EditorRetirement() { clear(); }

void EditorRetirement::release(std::unique_ptr<EditorView> view, std::unique_ptr<sdk::EditorModel> model) {
    if (!view) return;
    view->hide();
    if (auto* emulator = qobject_cast<EmulatorView*>(view.get())) emulator->setSidePanel(nullptr);
    if (model && model->type() == sdk::EditorType::Emulator &&
        runner_->phase() != runtime::EmulatorRunner::Phase::Idle) {
        auto entry = std::make_unique<Editor>();
        entry->model = std::move(model);
        entry->view = std::move(view);
        editors_.push_back(std::move(entry));
        // A probe may finish synchronously here; ownership is already safe.
        runner_->abandon();
        return;
    }
    view.reset();
}

void EditorRetirement::releaseSession(std::unique_ptr<sdk::Session> session) {
    if (session && isPending()) sessions_.push_back(std::move(session));
}

void EditorRetirement::clear() {
    editors_.clear();
    sessions_.clear();
}

}  // namespace rl::editors
