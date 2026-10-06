#include "setup_operations.hpp"

#include "sdk/i18n.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QTemporaryDir>

#include <string>

#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <tlhelp32.h>

namespace rl::setup {

const QString kUninstallerName = QStringLiteral("Uninstall.exe");

namespace {

const QString kProduct = QStringLiteral("Retrolarium Studio");
const QString kStudioName = QStringLiteral("retrolarium-studio.exe");
// HKEY_CURRENT_USER subkeys. Reading and removing them goes through Win32:
// QSettings creates a native key as soon as it opens one.
const QString kUninstallKey = QStringLiteral("Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\RetrolariumStudio");
const QString kExtensionKey = QStringLiteral("Software\\Classes\\.rlj");
const QString kProjectTypeKey = QStringLiteral("Software\\Classes\\Retrolarium.Project");
const QString kSettingsKey = QStringLiteral("Software\\Retrolarium\\Retrolarium Studio");
const QString kProjectType = QStringLiteral("Retrolarium.Project");
// What the studio writes in the user's MAME beside it: kept across updates.
const QStringList kKeptPaths{QStringLiteral("mame/roms"), QStringLiteral("mame/cfg"), QStringLiteral("mame/nvram")};

QString nativePath(const QString& path) {
    return QDir::toNativeSeparators(QDir::cleanPath(path));
}

QString registryString(const QString& key, const QString& value) {
    wchar_t data[2048] = {};
    DWORD size = sizeof(data);
    const std::wstring keyWide = key.toStdWString();
    const std::wstring valueWide = value.toStdWString();
    if (RegGetValueW(HKEY_CURRENT_USER, keyWide.c_str(), value.isEmpty() ? nullptr : valueWide.c_str(), RRF_RT_REG_SZ,
                     nullptr, data, &size) != ERROR_SUCCESS) {
        return {};
    }
    return QString::fromWCharArray(data);
}

void deleteKey(const QString& key) {
    RegDeleteTreeW(HKEY_CURRENT_USER, key.toStdWString().c_str());
    RegDeleteKeyW(HKEY_CURRENT_USER, key.toStdWString().c_str());
}

// Written through QSettings, which takes the full path.
QString writable(const QString& key) {
    return QStringLiteral("HKEY_CURRENT_USER\\") + key;
}

QString shellFolder(int csidl) {
    wchar_t path[MAX_PATH] = {};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, csidl | CSIDL_FLAG_CREATE, nullptr, SHGFP_TYPE_CURRENT, path))) {
        return QDir::fromNativeSeparators(QString::fromWCharArray(path));
    }
    return {};
}

bool inside(const QString& path, const QString& directory) {
    const QString root = QDir::cleanPath(QDir(directory).absolutePath()) + QLatin1Char('/');
    return QDir::cleanPath(QDir::fromNativeSeparators(path)).startsWith(root, Qt::CaseInsensitive);
}

bool createShortcut(const QString& shortcutPath, const QString& target, const QString& arguments,
                    const QString& description, QString* error) {
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool uninitialize = SUCCEEDED(initialized);
    IShellLinkW* link = nullptr;
    HRESULT result = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW,
                                      reinterpret_cast<void**>(&link));
    if (SUCCEEDED(result)) result = link->SetPath(reinterpret_cast<const wchar_t*>(nativePath(target).utf16()));
    if (SUCCEEDED(result) && !arguments.isEmpty()) {
        result = link->SetArguments(reinterpret_cast<const wchar_t*>(arguments.utf16()));
    }
    if (SUCCEEDED(result)) {
        result = link->SetWorkingDirectory(
            reinterpret_cast<const wchar_t*>(nativePath(QFileInfo(target).absolutePath()).utf16()));
    }
    if (SUCCEEDED(result)) result = link->SetDescription(reinterpret_cast<const wchar_t*>(description.utf16()));
    if (SUCCEEDED(result)) result = link->SetIconLocation(reinterpret_cast<const wchar_t*>(nativePath(target).utf16()), 0);
    IPersistFile* persist = nullptr;
    if (SUCCEEDED(result)) result = link->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&persist));
    if (SUCCEEDED(result)) {
        QDir().mkpath(QFileInfo(shortcutPath).absolutePath());
        result = persist->Save(reinterpret_cast<const wchar_t*>(nativePath(shortcutPath).utf16()), TRUE);
    }
    if (persist) persist->Release();
    if (link) link->Release();
    if (uninitialize) CoUninitialize();
    if (FAILED(result)) {
        if (error) {
            *error = rlTr("setup", "Could not create the shortcut %1.").arg(nativePath(shortcutPath));
        }
        return false;
    }
    return true;
}

QString startMenuDirectory() {
    const QString programs = shellFolder(CSIDL_PROGRAMS);
    return programs.isEmpty() ? QString() : QDir(programs).filePath(kProduct);
}

void removeShortcuts() {
    const QString menu = startMenuDirectory();
    if (!menu.isEmpty()) {
        // Both languages' names, so a reinstall in the other one leaves none behind.
        for (const QString& name : {kProduct + QStringLiteral(".lnk"), QStringLiteral("Uninstall Retrolarium Studio.lnk"),
                                    QStringLiteral("Удалить Retrolarium Studio.lnk")}) {
            QFile::remove(QDir(menu).filePath(name));
        }
        QDir().rmdir(menu);
    }
    QFile::remove(desktopShortcutPath());
}

bool registerInstallation(const QString& directory, const QString& version, quint64 bytes, QString* error) {
    QSettings uninstall(writable(kUninstallKey), QSettings::NativeFormat);
    uninstall.setValue(QStringLiteral("DisplayName"), kProduct);
    uninstall.setValue(QStringLiteral("DisplayVersion"), version);
    uninstall.setValue(QStringLiteral("Publisher"), QStringLiteral("Retrolarium"));
    uninstall.setValue(QStringLiteral("InstallLocation"), nativePath(directory));
    uninstall.setValue(QStringLiteral("DisplayIcon"), nativePath(QDir(directory).filePath(kStudioName)));
    uninstall.setValue(QStringLiteral("UninstallString"),
                       QStringLiteral("\"%1\" --uninstall").arg(nativePath(QDir(directory).filePath(kUninstallerName))));
    uninstall.setValue(QStringLiteral("EstimatedSize"), static_cast<quint32>((bytes + 1023) / 1024));
    uninstall.setValue(QStringLiteral("NoModify"), 1);
    uninstall.sync();

    // A game's .rlj opens in the studio.
    const QString studio = nativePath(QDir(directory).filePath(kStudioName));
    QSettings extension(writable(kExtensionKey), QSettings::NativeFormat);
    extension.setValue(QStringLiteral("Default"), kProjectType);
    extension.sync();
    QSettings type(writable(kProjectTypeKey), QSettings::NativeFormat);
    type.setValue(QStringLiteral("Default"), rlTr("setup", "Retrolarium project"));
    type.setValue(QStringLiteral("DefaultIcon/Default"), studio + QStringLiteral(",0"));
    type.setValue(QStringLiteral("shell/open/command/Default"), QStringLiteral("\"%1\" \"%2\"").arg(studio, QStringLiteral("%1")));
    type.sync();
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    if (uninstall.status() != QSettings::NoError || type.status() != QSettings::NoError) {
        if (error) *error = rlTr("setup", "Could not register Retrolarium Studio with Windows.");
        return false;
    }
    return true;
}

void unregisterInstallation(const QString& directory) {
    deleteKey(kUninstallKey);
    // The association goes only when it still opens this installation.
    const QString command = registryString(kProjectTypeKey + QStringLiteral("\\shell\\open\\command"), QString());
    if (inside(command.section(QLatin1Char('"'), 1, 1), directory)) {
        deleteKey(kProjectTypeKey);
        if (registryString(kExtensionKey, QString()) == kProjectType) deleteKey(kExtensionKey);
        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    }
}

// Copies what the user added to the old installation (the Disk System BIOS,
// MAME's settings) into the new one.
void keepUserFiles(const QString& from, const QString& to) {
    for (const QString& kept : kKeptPaths) {
        const QString source = QDir(from).filePath(kept);
        if (!QDir(source).exists()) continue;
        QDirIterator it(source, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString file = it.next();
            const QString target = QDir(to).filePath(QDir(from).relativeFilePath(file));
            if (QFile::exists(target)) continue;
            QDir().mkpath(QFileInfo(target).absolutePath());
            QFile::copy(file, target);
        }
    }
}

}  // namespace

namespace {
QString gLanguage = QStringLiteral("en");
}  // namespace

void setLanguage(const QString& code) {
    gLanguage = code;
}

QString language() {
    return gLanguage;
}

QString defaultInstallDirectory() {
    QString base = shellFolder(CSIDL_LOCAL_APPDATA);
    if (base.isEmpty()) base = QDir::homePath();
    return QDir(base).filePath(QStringLiteral("Programs/Retrolarium Studio"));
}

QString installedDirectory() {
    return QDir::fromNativeSeparators(registryString(kUninstallKey, QStringLiteral("InstallLocation"))).trimmed();
}

QString installedVersion() {
    return registryString(kUninstallKey, QStringLiteral("DisplayVersion")).trimmed();
}

QString desktopShortcutPath() {
    const QString desktop = shellFolder(CSIDL_DESKTOPDIRECTORY);
    return desktop.isEmpty() ? QString() : QDir(desktop).filePath(kProduct + QStringLiteral(".lnk"));
}

QString gamesDirectory() {
    return QDir(shellFolder(CSIDL_PERSONAL)).filePath(QStringLiteral("Retrolarium"));
}

QStringList runningPrograms(const QString& directory) {
    QStringList running;
    if (directory.isEmpty() || !QDir(directory).exists()) return running;
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return running;
    PROCESSENTRY32W entry = {};
    entry.dwSize = sizeof(entry);
    const DWORD self = GetCurrentProcessId();
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (entry.th32ProcessID == self) continue;
            const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
            if (!process) continue;
            wchar_t image[MAX_PATH * 2] = {};
            DWORD length = MAX_PATH * 2;
            if (QueryFullProcessImageNameW(process, 0, image, &length) &&
                inside(QString::fromWCharArray(image, static_cast<int>(length)), directory)) {
                const QString name = QString::fromWCharArray(entry.szExeFile);
                if (!running.contains(name, Qt::CaseInsensitive)) running << name;
            }
            CloseHandle(process);
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return running;
}

bool replaceableDirectory(const QString& directory) {
    const QDir dir(directory);
    if (!dir.exists() || dir.isEmpty(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot)) return true;
    return QFile::exists(dir.filePath(kStudioName)) && QFile::exists(dir.filePath(kUninstallerName));
}

bool install(const Request& request, const Progress& progress, QString* error) {
    const QString target = QDir::cleanPath(QDir(request.installDirectory).absolutePath());
    const QString parent = QFileInfo(target).absolutePath();
    if (!runningPrograms(target).isEmpty()) {
        if (error) *error = rlTr("setup", "Retrolarium Studio is still running. Close it and try again.");
        return false;
    }
    if (!replaceableDirectory(target)) {
        if (error) *error = rlTr("setup", "%1 holds other files. Choose an empty folder.").arg(nativePath(target));
        return false;
    }
    if (!QDir().mkpath(parent)) {
        if (error) *error = rlTr("setup", "Could not create the installation folder.");
        return false;
    }
    QTemporaryDir staging(QDir(parent).filePath(QStringLiteral(".Retrolarium Studio-setup-XXXXXX")));
    if (!staging.isValid()) {
        if (error) *error = rlTr("setup", "Could not create a temporary folder next to the installation.");
        return false;
    }
    if (!InstallerPackage::extract(
            request.packagePath, request.manifest, staging.path(),
            [&progress](quint64 done, quint64 total, const QString& path) {
                if (progress) {
                    progress(2 + static_cast<int>(total ? done * 76 / total : 76),
                             rlTr("setup", "Unpacking %1").arg(nativePath(path)));
                }
            },
            error) ||
        !InstallerPackage::copyStub(request.packagePath, request.manifest.stubSize,
                                    QDir(staging.path()).filePath(kUninstallerName), error)) {
        return false;
    }
    quint64 bytes = request.manifest.stubSize;
    for (const PayloadEntry& entry : request.manifest.files) bytes += entry.size;

    if (progress) progress(80, rlTr("setup", "Replacing the earlier version"));
    const bool update = QDir(target).exists();
    const QString previous = target + QStringLiteral(".previous-%1").arg(GetCurrentProcessId());
    if (update) {
        keepUserFiles(target, staging.path());
        if (!QDir().rename(target, previous)) {
            if (error) *error = rlTr("setup", "Could not replace the earlier version: a program may still use its files.");
            return false;
        }
    }
    staging.setAutoRemove(false);
    if (!QDir().rename(staging.path(), target)) {
        QDir(staging.path()).removeRecursively();
        if (update) QDir().rename(previous, target);
        if (error) *error = rlTr("setup", "Could not move the new files into place.");
        return false;
    }
    auto rollback = [&] {
        QDir(target).removeRecursively();
        if (update) QDir().rename(previous, target);
    };

    if (progress) progress(88, rlTr("setup", "Creating shortcuts"));
    const QString menu = startMenuDirectory();
    const QString studio = QDir(target).filePath(kStudioName);
    removeShortcuts();
    if (menu.isEmpty() ||
        !createShortcut(QDir(menu).filePath(kProduct + QStringLiteral(".lnk")), studio, {}, kProduct, error) ||
        !createShortcut(QDir(menu).filePath(rlTr("setup", "Uninstall Retrolarium Studio.lnk")),
                        QDir(target).filePath(kUninstallerName), QStringLiteral("--uninstall"),
                        rlTr("setup", "Uninstall Retrolarium Studio"), error) ||
        (request.desktopShortcut && !createShortcut(desktopShortcutPath(), studio, {}, kProduct, error))) {
        rollback();
        return false;
    }
    if (progress) progress(94, rlTr("setup", "Registering with Windows"));
    if (!registerInstallation(target, request.manifest.version, bytes, error)) {
        rollback();
        return false;
    }
    // The studio starts in the language setup ran in.
    QSettings studioSettings(writable(kSettingsKey), QSettings::NativeFormat);
    studioSettings.setValue(QStringLiteral("ui/language"), language());

    if (update) QDir(previous).removeRecursively();
    if (progress) progress(100, rlTr("setup", "Retrolarium Studio is installed"));
    return true;
}

bool uninstall(const Request& request, const Progress& progress, QString* error, QString* warning) {
    const QString directory = QDir::cleanPath(QDir(request.installDirectory).absolutePath());
    if (!runningPrograms(directory).isEmpty()) {
        if (error) *error = rlTr("setup", "Retrolarium Studio is still running. Close it and try again.");
        return false;
    }
    if (progress) progress(10, rlTr("setup", "Removing shortcuts"));
    removeShortcuts();
    unregisterInstallation(directory);

    const QString self = QCoreApplication::applicationFilePath();
    const QFileInfoList entries =
        QDir(directory).entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot);
    QStringList left;
    for (int i = 0; i < entries.size(); ++i) {
        const QFileInfo& entry = entries.at(i);
        if (progress) {
            progress(20 + i * 60 / qMax(1, entries.size()), rlTr("setup", "Removing %1").arg(entry.fileName()));
        }
        if (sameFile(entry.absoluteFilePath(), self)) continue;
        const bool removed = entry.isDir() ? QDir(entry.absoluteFilePath()).removeRecursively()
                                           : QFile::remove(entry.absoluteFilePath());
        if (!removed) left << entry.fileName();
    }
    if (!left.isEmpty() && warning) {
        *warning = rlTr("setup", "Some files stayed in %1: %2. Delete them by hand.")
                       .arg(nativePath(directory), left.join(QStringLiteral(", ")));
    }
    if (request.removeUserData) {
        if (progress) progress(90, rlTr("setup", "Removing your games and settings"));
        QDir(gamesDirectory()).removeRecursively();
        deleteKey(kSettingsKey);
        // And the organisation's key, while nothing else is under it.
        RegDeleteKeyW(HKEY_CURRENT_USER, L"Software\\Retrolarium");
    }
    if (!inside(self, directory)) QDir().rmdir(directory);
    if (progress) progress(100, rlTr("setup", "Retrolarium Studio has been removed"));
    return true;
}

bool launchStudio(const QString& directory, QString* error) {
    const QString studio = nativePath(QDir(directory).filePath(kStudioName));
    const HINSTANCE result = ShellExecuteW(nullptr, L"open", reinterpret_cast<const wchar_t*>(studio.utf16()), nullptr,
                                           reinterpret_cast<const wchar_t*>(nativePath(directory).utf16()),
                                           SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) <= 32) {
        if (error) *error = rlTr("setup", "Windows could not start the studio (error %1).")
                                .arg(reinterpret_cast<INT_PTR>(result));
        return false;
    }
    return true;
}

bool sameFile(const QString& left, const QString& right) {
    return QFileInfo(left).canonicalFilePath().compare(QFileInfo(right).canonicalFilePath(), Qt::CaseInsensitive) == 0;
}

bool scheduleSelfRemoval(const QString& self, const QString& directory, QString* error) {
    QString shell = qEnvironmentVariable("COMSPEC");
    if (shell.isEmpty()) shell = QStringLiteral("cmd.exe");
    // Paths travel in environment variables, which cmd expands once, so a
    // '%' in a folder name stays literal. The helper retries for two minutes
    // while the uninstaller exits or an antivirus holds the file.
    const QString suffix = QString::number(GetCurrentProcessId());
    const std::wstring selfVariable = QStringLiteral("RETROLARIUM_UNINSTALL_SELF_%1").arg(suffix).toStdWString();
    const std::wstring directoryVariable = QStringLiteral("RETROLARIUM_UNINSTALL_DIR_%1").arg(suffix).toStdWString();
    if (!SetEnvironmentVariableW(selfVariable.c_str(), nativePath(self).toStdWString().c_str()) ||
        !SetEnvironmentVariableW(directoryVariable.c_str(), nativePath(directory).toStdWString().c_str())) {
        if (error) *error = rlTr("setup", "The studio was removed, but setup could not remove itself.");
        return false;
    }
    const QString selfReference = QLatin1Char('%') + QString::fromStdWString(selfVariable) + QLatin1Char('%');
    const QString directoryReference = QLatin1Char('%') + QString::fromStdWString(directoryVariable) + QLatin1Char('%');
    const QString command = QStringLiteral(
                                "for /L %i in (1,1,120) do @(del /f /q \"%1\" >nul 2>nul & "
                                "if not exist \"%1\" (rmdir \"%2\" >nul 2>nul & exit /b 0) & "
                                "ping 127.0.0.1 -n 2 >nul)")
                                .arg(selfReference, directoryReference);
    const std::wstring parameters = QStringLiteral("/D /C \"%1\"").arg(command).toStdWString();
    const std::wstring program = shell.toStdWString();
    // Not the uninstaller's directory as the working one: Windows would not
    // remove a directory a running process sits in.
    const std::wstring working = QDir::toNativeSeparators(QDir::tempPath()).toStdWString();
    SHELLEXECUTEINFOW execute = {};
    execute.cbSize = sizeof(execute);
    execute.fMask = SEE_MASK_FLAG_NO_UI | SEE_MASK_NOASYNC;
    execute.lpFile = program.c_str();
    execute.lpParameters = parameters.c_str();
    execute.lpDirectory = working.c_str();
    execute.nShow = SW_HIDE;
    const BOOL started = ShellExecuteExW(&execute);
    SetEnvironmentVariableW(selfVariable.c_str(), nullptr);
    SetEnvironmentVariableW(directoryVariable.c_str(), nullptr);
    if (!started) {
        if (error) *error = rlTr("setup", "The studio was removed, but setup could not remove itself.");
        return false;
    }
    return true;
}

}  // namespace rl::setup
