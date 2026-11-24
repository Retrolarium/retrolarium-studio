#pragma once

#include <QByteArray>
#include <QString>

#ifdef RETROLARIUM_PROJECT_WRITER_TEST
#include <functional>
#endif

namespace rl::sdk {

// Atomically writes an absolute path beneath an absolute boundary. On Windows,
// the held project directory chain denies delete sharing, preventing renames on
// filesystems that honor those sharing rules. POSIX uses descriptor-relative,
// no-follow traversal but cannot prevent another process moving an open directory.
bool writeFileWithin(const QString& boundary, const QString& path, const QByteArray& data, QString* error);

enum class ProjectWriterTestStage { ParentOpen, BeforeWrite };
#ifdef RETROLARIUM_PROJECT_WRITER_TEST
void setProjectWriterTestHook(std::function<void(ProjectWriterTestStage)> hook);
#endif

}  // namespace rl::sdk
