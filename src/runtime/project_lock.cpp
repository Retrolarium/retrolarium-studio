#include "runtime/project_lock.hpp"

#include <QDir>
#include <QFileInfo>
#include <QLockFile>

namespace rl::runtime {

ProjectLock::ProjectLock(std::unique_ptr<QLockFile> file, const QString& path) : file_(std::move(file)), path_(path) {}

ProjectLock::~ProjectLock() {
    if (file_) {
        file_->unlock();
    }
}

QString ProjectLock::lockPath(const QString& projectPath) {
    return QDir(QFileInfo(projectPath).absolutePath()).filePath(QStringLiteral(".retrolarium/studio.lock"));
}

std::unique_ptr<ProjectLock> ProjectLock::acquire(const QString& projectPath, bool* held, qint64* ownerPid) {
    if (held) *held = false;
    if (ownerPid) *ownerPid = 0;
    const QString path = lockPath(projectPath);
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        return nullptr;
    }
    auto file = std::make_unique<QLockFile>(path);
    // Never stale by age: an open project may stay open for days. A lock
    // whose process is gone is still taken over.
    file->setStaleLockTime(0);
    if (file->tryLock(0)) {
        return std::unique_ptr<ProjectLock>(new ProjectLock(std::move(file), path));
    }
    if (file->error() == QLockFile::LockFailedError) {
        if (held) *held = true;
        qint64 pid = 0;
        QString hostname;
        QString application;
        if (file->getLockInfo(&pid, &hostname, &application) && ownerPid) {
            *ownerPid = pid;
        }
    }
    return nullptr;
}

}  // namespace rl::runtime
