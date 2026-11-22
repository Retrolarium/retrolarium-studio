// The project lock: a second lock on the same project fails and names the
// process holding it, the lock file sits in .retrolarium beside the .rlj,
// and a released lock can be taken again. Needs no project.
//   project_lock_test

#include "runtime/project_lock.hpp"

#include <QCoreApplication>
#include <QFileInfo>
#include <QTemporaryDir>

#include <iostream>

using rl::runtime::ProjectLock;

namespace {

int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir dir;
    if (!dir.isValid()) {
        std::cerr << "FAIL: no temporary folder\n";
        return 1;
    }
    const QString project = dir.filePath(QStringLiteral("game/.retrolarium/jp/game-jp.rlj"));
    const QString other = dir.filePath(QStringLiteral("other/other.rlj"));

    expect(
        ProjectLock::lockPath(project) == dir.filePath(QStringLiteral("game/.retrolarium/jp/.retrolarium/studio.lock")),
        "the lock sits in .retrolarium beside the project");

    bool held = true;
    qint64 owner = -1;
    std::unique_ptr<ProjectLock> first = ProjectLock::acquire(project, &held, &owner);
    expect(first != nullptr && !held, "the first lock is taken");
    expect(first && QFileInfo::exists(first->path()), "the lock file is written");

    std::unique_ptr<ProjectLock> second = ProjectLock::acquire(project, &held, &owner);
    expect(second == nullptr, "a second lock on the same project fails");
    expect(held, "the project is reported as held");
    expect(owner == QCoreApplication::applicationPid(), "the holder's process is named");

    std::unique_ptr<ProjectLock> elsewhere = ProjectLock::acquire(other, &held, &owner);
    expect(elsewhere != nullptr && !held, "another project locks independently");

    first.reset();
    std::unique_ptr<ProjectLock> again = ProjectLock::acquire(project, &held, &owner);
    expect(again != nullptr && !held, "a released lock is taken again");

    if (failures != 0) return 1;
    std::cout << "project lock: one studio per project holds\n";
    return 0;
}
