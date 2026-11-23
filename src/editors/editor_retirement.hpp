#pragma once

#include <QObject>

#include <memory>
#include <vector>

namespace rl::sdk {
class EditorModel;
class Session;
}  // namespace rl::sdk
namespace rl::runtime {
class EmulatorRunner;
}

namespace rl::editors {
class EditorView;

// A closing project's embedded emulator keeps its native window, model and
// session until the process has flushed its movie and stopped. No reparenting:
// on Windows that could recreate the native window MAME is still drawing in.
class EditorRetirement final : public QObject {
public:
    explicit EditorRetirement(runtime::EmulatorRunner* runner);
    ~EditorRetirement() override;

    void release(std::unique_ptr<EditorView> view, std::unique_ptr<sdk::EditorModel> model);
    void releaseSession(std::unique_ptr<sdk::Session> session);
    bool isPending() const { return !editors_.empty(); }

private:
    void clear();
    struct Editor;
    runtime::EmulatorRunner* runner_;
    std::vector<std::unique_ptr<Editor>> editors_;
    std::vector<std::unique_ptr<sdk::Session>> sessions_;
};
}  // namespace rl::editors
