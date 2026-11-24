// Failure injection and a deterministic parent-directory swap for the bounded
// atomic writer. The hook is compiled only into this test, never Studio itself.
#include "sdk/project_writer.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#ifndef Q_OS_WIN
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <iostream>
#include <stdexcept>

namespace {

int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

QByteArray readAll(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}

void failedWriteKeepsOldFile(const QString& root) {
    const QString path = root + QStringLiteral("/old.json");
    expect(rl::sdk::writeFileWithin(root, path, QByteArray("old"), nullptr), "create the old-document fixture");
    rl::sdk::setProjectWriterTestHook([](rl::sdk::ProjectWriterTestStage stage) {
        if (stage == rl::sdk::ProjectWriterTestStage::BeforeWrite) throw std::runtime_error("injected write failure");
    });
    QString error;
    expect(!rl::sdk::writeFileWithin(root, path, QByteArray("new"), &error) && !error.isEmpty(),
           "an injected write failure is reported");
    rl::sdk::setProjectWriterTestHook({});
    expect(readAll(path) == QByteArray("old"), "a failed write preserves the old document");
}

void parentMoveModelIsExplicit(const QString& root) {
    const QString parent = root + QStringLiteral("/build");
    // The destination is a sibling of the project, not another directory in it.
    const QString moved = QFileInfo(root).dir().filePath(QStringLiteral("moved-build"));
    QDir().mkpath(parent);
    bool movedOutside = false;
    rl::sdk::setProjectWriterTestHook([&](rl::sdk::ProjectWriterTestStage stage) {
        if (stage == rl::sdk::ProjectWriterTestStage::ParentOpen) movedOutside = QDir().rename(parent, moved);
    });
    QString error;
    const bool written =
        rl::sdk::writeFileWithin(root, parent + QStringLiteral("/demo.json"), QByteArray("safe"), &error);
    rl::sdk::setProjectWriterTestHook({});
#ifdef Q_OS_WIN
    expect(!movedOutside, "Windows directory guards prevent moving the selected parent outside");
    expect(written, "a Windows write succeeds when the parent move is denied");
    expect(readAll(parent + QStringLiteral("/demo.json")) == QByteArray("safe"),
           "the Windows document remains inside the project");
    expect(!QFileInfo::exists(moved + QStringLiteral("/demo.json")), "no Windows document appears outside");
#else
    expect(movedOutside, "the POSIX fixture moves the selected parent outside");
    if (movedOutside) {
        // This expectation deliberately records the remaining POSIX limitation:
        // descriptor-relative operations follow the moved directory object.
        expect(written, "the POSIX descriptor remains usable after the parent move");
        expect(readAll(moved + QStringLiteral("/demo.json")) == QByteArray("safe"),
               "POSIX follows the moved directory; no boundary guarantee is claimed");
    }
#endif
}

#ifndef Q_OS_WIN
void permissionsArePreserved(const QString& root) {
    const QString path = root + QStringLiteral("/private.json");
    const QByteArray native = QFile::encodeName(path);
    const int input = ::open(native.constData(), O_WRONLY | O_CREAT | O_EXCL, 0600);
    expect(input >= 0, "create the mode-0600 fixture");
    if (input < 0) return;
    expect(::write(input, "old", 3) == 3, "write the mode-0600 fixture");
    ::close(input);
    expect(::chmod(native.constData(), 0600) == 0, "set an exact mode despite the process umask");

    QString error;
    expect(rl::sdk::writeFileWithin(root, path, QByteArray("new"), &error), "replace the mode-0600 document");
    struct stat status{};
    const int inspected = ::stat(native.constData(), &status);
    expect(inspected == 0, "inspect the replaced document mode");
    if (inspected == 0) expect((status.st_mode & 0777) == 0600, "atomic replacement preserves mode 0600");
    expect(readAll(path) == QByteArray("new"), "the permission regression contains the new document");
}
#endif

}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir folder;
    const QString root = folder.path() + QStringLiteral("/project");
    QDir().mkpath(root);
    failedWriteKeepsOldFile(root);
    parentMoveModelIsExplicit(root);
#ifndef Q_OS_WIN
    permissionsArePreserved(root);
#endif
    if (failures) return 1;
    std::cout << "project writer: rollback, platform move model and permissions hold\n";
    return 0;
}
