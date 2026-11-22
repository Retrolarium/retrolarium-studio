#pragma once

#include <QString>
#include <QtGlobal>

#include <memory>

class QLockFile;

namespace rl::runtime {

// Keeps a project to one studio: a lock file beside the project
// (.retrolarium/studio.lock in the .rlj's folder; the kit leaves .retrolarium
// alone), held while the project is open. A double-click on an .rlj starts
// another studio, which then refuses the project. The lock of a studio that
// crashed is taken over: QLockFile sees that the process that wrote it is
// gone.
class ProjectLock final {
public:
    ~ProjectLock();
    ProjectLock(const ProjectLock&) = delete;
    ProjectLock& operator=(const ProjectLock&) = delete;

    // The lock for the project at `projectPath`, or null. Null with `held`
    // true: another studio (process `ownerPid`, 0 if unknown) has it, or this
    // one through another lock. Null with `held` false: the lock file could
    // not be written (a read-only folder); the project may open unlocked.
    static std::unique_ptr<ProjectLock> acquire(const QString& projectPath, bool* held, qint64* ownerPid);
    static QString lockPath(const QString& projectPath);

    QString path() const { return path_; }

private:
    ProjectLock(std::unique_ptr<QLockFile> file, const QString& path);

    std::unique_ptr<QLockFile> file_;
    QString path_;
};

}  // namespace rl::runtime
