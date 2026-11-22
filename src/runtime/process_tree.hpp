#pragma once

#include <memory>

class QProcess;

namespace rl::runtime {

// A program and every process it starts, ended together. On Windows the
// program starts inside a job object: terminate() ends every process in it,
// and so does destroying the tree (the job kills its processes when its last
// handle closes, also when the studio itself goes down). Elsewhere (builds
// for development) only the program itself is ended by its QProcess: Qt 5
// offers no hook to start it in a process group of its own.
class ProcessTree final {
public:
    ProcessTree();
    ~ProcessTree();
    ProcessTree(const ProcessTree&) = delete;
    ProcessTree& operator=(const ProcessTree&) = delete;

    // Before process->start(): the program starts inside the tree (Windows
    // 10 and later), before it can start anything of its own.
    void prepare(QProcess* process);
    // After process->start(): a program that could not start inside the
    // tree joins it now.
    void started(QProcess* process);
    // Ends every process in the tree at once.
    void terminate();

private:
    struct Native;
    std::unique_ptr<Native> native_;
};

}  // namespace rl::runtime
