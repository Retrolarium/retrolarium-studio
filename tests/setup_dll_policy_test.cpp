#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>

#include <windows.h>

#include <iostream>

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir temporary;
    const QDir fixtures(QCoreApplication::applicationDirPath() + QStringLiteral("/test-fixtures/dll-policy"));
    const auto fail = [](const char* message) {
        std::cerr << "FAIL: " << message << '\n';
        return 1;
    };
    if (!temporary.isValid()) return fail("cannot create a private DLL policy directory");
    for (const auto& name : {QStringLiteral("version.dll"), QStringLiteral("setup_dll_control.exe"),
                             QStringLiteral("setup_dll_guarded.exe")}) {
        if (!QFile::copy(fixtures.filePath(name), temporary.filePath(name))) return fail("missing DLL policy fixture");
    }
    const auto run = [&temporary](const QString& name) {
        QProcess process;
        process.setCreateProcessArgumentsModifier(
            [](QProcess::CreateProcessArguments* arguments) { arguments->flags |= CREATE_NO_WINDOW; });
        process.setWorkingDirectory(temporary.path());
        process.start(temporary.filePath(name), QStringList{});
        if (!process.waitForFinished(15000)) {
            process.kill();
            process.waitForFinished(1000);
            return false;
        }
        return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
    };
    const QString marker = temporary.filePath(QStringLiteral("version.dll.loaded"));
    if (!run(QStringLiteral("setup_dll_control.exe")) || !QFile::exists(marker))
        return fail("the unprotected pre-main control did not load the canary");
    if (!QFile::remove(marker)) return fail("cannot reset the canary marker");
    if (!run(QStringLiteral("setup_dll_guarded.exe"))) return fail("the guarded pre-main control could not start");
    if (QFile::exists(marker)) return fail("the guarded pre-main control loaded the local VERSION.dll");
    std::cout << "DLL policy: positive control loads the canary; protected imports use System32\n";
    return 0;
}
