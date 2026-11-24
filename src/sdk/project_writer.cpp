#include "sdk/project_writer.hpp"

#include "sdk/i18n.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUuid>

#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace rl::sdk {
namespace {

#ifdef RETROLARIUM_PROJECT_WRITER_TEST
std::function<void(ProjectWriterTestStage)>& testHook() {
    static std::function<void(ProjectWriterTestStage)> hook;
    return hook;
}

void testStage(ProjectWriterTestStage stage) {
    if (testHook()) testHook()(stage);
}
#else
void testStage(ProjectWriterTestStage) {}
#endif

[[noreturn]] void fail(const QString& path) {
    QString message = rlTr("studio", "Cannot write %1.").arg(path);
#ifdef Q_OS_WIN
    message += QStringLiteral(" (error %1)").arg(GetLastError());
#endif
    throw std::runtime_error(message.toStdString());
}

#ifdef Q_OS_WIN
[[noreturn]] void failSystem(const QString& path) {
    const QString message =
        rlTr("studio", "Cannot write %1.").arg(path) + QStringLiteral(" (error %1)").arg(GetLastError());
    throw std::runtime_error(message.toStdString());
}
#endif

bool within(const QString& path, const QString& root) {
#ifdef Q_OS_WIN
    const auto sensitivity = Qt::CaseInsensitive;
#else
    const auto sensitivity = Qt::CaseSensitive;
#endif
    const QString prefix = root.endsWith(QLatin1Char('/')) ? root : root + QLatin1Char('/');
    return path.compare(root, sensitivity) == 0 || path.startsWith(prefix, sensitivity);
}

QString temporaryName() {
    return QStringLiteral(".retrolarium-") + QUuid::createUuid().toString(QUuid::WithoutBraces) +
           QStringLiteral(".tmp");
}

#ifdef Q_OS_WIN

using NTSTATUS = LONG;
constexpr ULONG kFileDirectory = 0x00000001;
constexpr ULONG kFileNonDirectory = 0x00000040;
constexpr ULONG kFileSynchronousNonAlert = 0x00000020;
constexpr ULONG kFileOpen = 1;
constexpr ULONG kFileCreate = 2;
constexpr ULONG kFileOpenIf = 3;
constexpr ACCESS_MASK kFileTraverse = 0x20;
struct UnicodeString {
    USHORT length;
    USHORT maximumLength;
    wchar_t* buffer;
};
struct ObjectAttributes {
    ULONG length;
    HANDLE rootDirectory;
    UnicodeString* objectName;
    ULONG attributes;
    void* securityDescriptor;
    void* securityQualityOfService;
};
struct IoStatusBlock {
    NTSTATUS status;
    ULONG_PTR information;
};
using NtCreateFileFn = NTSTATUS(NTAPI*)(HANDLE*, ACCESS_MASK, ObjectAttributes*, IoStatusBlock*, LARGE_INTEGER*, ULONG,
                                        ULONG, ULONG, ULONG, void*, ULONG);
using RtlNtStatusToDosErrorFn = ULONG(NTAPI*)(NTSTATUS);
using NtSetInformationFileFn = NTSTATUS(NTAPI*)(HANDLE, IoStatusBlock*, void*, ULONG, ULONG);

NtCreateFileFn ntCreateFile() {
    static const auto function =
        reinterpret_cast<NtCreateFileFn>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtCreateFile"));
    if (!function) throw std::runtime_error("NtCreateFile is unavailable");
    return function;
}

NtSetInformationFileFn ntSetInformationFile() {
    static const auto function = reinterpret_cast<NtSetInformationFileFn>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtSetInformationFile"));
    if (!function) throw std::runtime_error("NtSetInformationFile is unavailable");
    return function;
}

ULONG ntError(NTSTATUS status) {
    static const auto convert = reinterpret_cast<RtlNtStatusToDosErrorFn>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlNtStatusToDosError"));
    if (convert) SetLastError(convert(status));
    return convert ? convert(status) : ERROR_GEN_FAILURE;
}

std::wstring nativeAbsolute(const QString& path) {
    QString native = QDir::toNativeSeparators(QDir::cleanPath(path));
    if (!native.startsWith(QStringLiteral("\\\\?\\"))) {
        native = native.startsWith(QStringLiteral("\\\\")) ? QStringLiteral("\\\\?\\UNC\\") + native.mid(2)
                                                           : QStringLiteral("\\\\?\\") + native;
    }
    return native.toStdWString();
}

class Handle {
public:
    Handle() = default;
    explicit Handle(HANDLE value) : value_(value) {}
    ~Handle() {
        if (value_ != INVALID_HANDLE_VALUE) CloseHandle(value_);
    }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept : value_(other.value_) { other.value_ = INVALID_HANDLE_VALUE; }
    Handle& operator=(Handle&& other) noexcept {
        if (this != &other) {
            if (value_ != INVALID_HANDLE_VALUE) CloseHandle(value_);
            value_ = other.value_;
            other.value_ = INVALID_HANDLE_VALUE;
        }
        return *this;
    }
    HANDLE get() const { return value_; }
    HANDLE* receive() { return &value_; }

private:
    HANDLE value_ = INVALID_HANDLE_VALUE;
};

QString finalPath(HANDLE handle, const QString& path) {
    const DWORD required = GetFinalPathNameByHandleW(handle, nullptr, 0, 0);
    std::vector<wchar_t> buffer(required + 1);
    const DWORD written =
        required ? GetFinalPathNameByHandleW(handle, buffer.data(), static_cast<DWORD>(buffer.size()), 0) : 0;
    if (!written || written >= buffer.size()) fail(path);
    QString result = QString::fromWCharArray(buffer.data(), static_cast<int>(written));
    if (result.startsWith(QStringLiteral("\\\\?\\UNC\\"))) result = QStringLiteral("\\\\") + result.mid(8);
    else if (result.startsWith(QStringLiteral("\\\\?\\"))) result.remove(0, 4);
    return QDir::cleanPath(QDir::fromNativeSeparators(result));
}

Handle openRoot(const QString& path) {
    // Delete sharing also permits rename. Keep it disabled for every directory
    // guard, while still allowing unrelated readers and writers.
    Handle handle(CreateFileW(nativeAbsolute(path).c_str(), GENERIC_READ | GENERIC_WRITE | kFileTraverse,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS,
                              nullptr));
    if (handle.get() == INVALID_HANDLE_VALUE) fail(path);
    return handle;
}

Handle openRelative(HANDLE parent, const QString& name, bool directory, bool create, const QString& path) {
    std::wstring wide = name.toStdWString();
    if (wide.empty() || wide.size() > 32767 || name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\'))) {
        fail(path);
    }
    UnicodeString objectName{static_cast<USHORT>(wide.size() * sizeof(wchar_t)),
                             static_cast<USHORT>(wide.size() * sizeof(wchar_t)), wide.data()};
    constexpr ULONG kCaseInsensitive = 0x40;
    constexpr ULONG kDontReparse = 0x1000;
    ObjectAttributes attributes{
        sizeof(attributes), parent, &objectName, kCaseInsensitive | kDontReparse, nullptr, nullptr};
    IoStatusBlock status{};
    Handle handle;
    const ACCESS_MASK access =
        directory ? GENERIC_READ | GENERIC_WRITE | kFileTraverse | SYNCHRONIZE : GENERIC_WRITE | DELETE | SYNCHRONIZE;
    const ULONG options = (directory ? kFileDirectory : kFileNonDirectory) | kFileSynchronousNonAlert;
    // The temporary file is not a directory guard: retain delete sharing so
    // its handle can be renamed atomically over the destination.
    const ULONG share = FILE_SHARE_READ | FILE_SHARE_WRITE | (directory ? 0 : FILE_SHARE_DELETE);
    const NTSTATUS result =
        ntCreateFile()(handle.receive(), access, &attributes, &status, nullptr,
                       directory ? FILE_ATTRIBUTE_NORMAL : FILE_ATTRIBUTE_TEMPORARY, share,
                       create ? (directory ? kFileOpenIf : kFileCreate) : kFileOpen, options, nullptr, 0);
    if (result < 0) {
        ntError(result);
        failSystem(path);
    }
    return handle;
}

void removeTemporary(HANDLE file) {
    FILE_DISPOSITION_INFO disposition{TRUE};
    SetFileInformationByHandle(file, FileDispositionInfo, &disposition, sizeof(disposition));
}

struct FileRenameInformation {
    BOOLEAN replaceIfExists;
    HANDLE rootDirectory;
    DWORD fileNameLength;
    wchar_t fileName[1];
};

void renameTemporary(HANDLE file, HANDLE parent, const QString& name, const QString& path) {
    const std::wstring wide = name.toStdWString();
    const SIZE_T bytes = offsetof(FileRenameInformation, fileName) + wide.size() * sizeof(wchar_t);
    std::vector<unsigned char> storage(bytes, 0);
    auto* rename = reinterpret_cast<FileRenameInformation*>(storage.data());
    rename->replaceIfExists = TRUE;
    rename->rootDirectory = parent;
    rename->fileNameLength = static_cast<DWORD>(wide.size() * sizeof(wchar_t));
    std::copy(wide.begin(), wide.end(), rename->fileName);
    IoStatusBlock status{};
    constexpr ULONG kFileRenameInformation = 10;
    const NTSTATUS result =
        ntSetInformationFile()(file, &status, rename, static_cast<ULONG>(storage.size()), kFileRenameInformation);
    if (result < 0) {
        ntError(result);
        failSystem(path);
    }
}

void writeNative(const QString& boundary, const QString& path, const QString& relative, const QByteArray& data) {
    // The boundary stored by ProjectFile is already canonical. Comparing the
    // final path rejects a root replaced before opening. Keep the root and every
    // selected parent alive without delete sharing until replacement finishes:
    // on filesystems honoring Windows sharing rules, their names cannot then be
    // moved or deleted during the write.
    std::vector<Handle> directories;
    directories.push_back(openRoot(boundary));
    if (finalPath(directories.back().get(), boundary).compare(boundary, Qt::CaseInsensitive) != 0) fail(boundary);

    const QStringList components = relative.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (components.isEmpty()) fail(path);
    for (int i = 0; i + 1 < components.size(); ++i) {
        directories.push_back(openRelative(directories.back().get(), components.at(i), true, true, path));
    }
    const HANDLE parent = directories.back().get();
    testStage(ProjectWriterTestStage::ParentOpen);
    const QString temporary = temporaryName();
    Handle output = openRelative(parent, temporary, false, true, path);
    testStage(ProjectWriterTestStage::BeforeWrite);
    qint64 offset = 0;
    while (offset < data.size()) {
        const DWORD chunk = static_cast<DWORD>(std::min<qint64>(data.size() - offset, MAXDWORD));
        DWORD written = 0;
        if (!WriteFile(output.get(), data.constData() + offset, chunk, &written, nullptr) || written != chunk) {
            removeTemporary(output.get());
            fail(path);
        }
        offset += written;
    }
    if (!FlushFileBuffers(output.get())) {
        removeTemporary(output.get());
        fail(path);
    }
    try {
        renameTemporary(output.get(), parent, components.last(), path);
    } catch (...) {
        removeTemporary(output.get());
        throw;
    }
}

#else

class Descriptor {
public:
    Descriptor() = default;
    explicit Descriptor(int value) : value_(value) {}
    ~Descriptor() {
        if (value_ >= 0) ::close(value_);
    }
    Descriptor(const Descriptor&) = delete;
    Descriptor& operator=(const Descriptor&) = delete;
    Descriptor(Descriptor&& other) noexcept : value_(other.value_) { other.value_ = -1; }
    Descriptor& operator=(Descriptor&& other) noexcept {
        if (this != &other) {
            if (value_ >= 0) ::close(value_);
            value_ = other.value_;
            other.value_ = -1;
        }
        return *this;
    }
    int get() const { return value_; }

private:
    int value_ = -1;
};

Descriptor openDirectory(int parent, const QByteArray& name, bool create, const QString& path) {
    if (create && ::mkdirat(parent, name.constData(), 0777) != 0 && errno != EEXIST) fail(path);
    const int result = ::openat(parent, name.constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (result < 0) fail(path);
    return Descriptor(result);
}

void writeNative(const QString& boundary, const QString& path, const QString& relative, const QByteArray& data) {
    // Descriptor-relative traversal avoids path lookup and symlink swaps. POSIX
    // does not lock directory names: another process can still move an opened
    // directory, and subsequent operations then follow that directory object.
    Descriptor parent(openDirectory(AT_FDCWD, QFile::encodeName(boundary), false, boundary));
    const QStringList components = relative.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (components.isEmpty()) fail(path);
    for (int i = 0; i + 1 < components.size(); ++i) {
        Descriptor child = openDirectory(parent.get(), QFile::encodeName(components.at(i)), true, path);
        parent = std::move(child);
    }
    testStage(ProjectWriterTestStage::ParentOpen);
    const QByteArray target = QFile::encodeName(components.last());
    struct stat targetStatus{};
    bool preservePermissions = false;
    mode_t permissions = 0;
    if (::fstatat(parent.get(), target.constData(), &targetStatus, AT_SYMLINK_NOFOLLOW) == 0) {
        preservePermissions = S_ISREG(targetStatus.st_mode);
        permissions = static_cast<mode_t>(targetStatus.st_mode & 07777);
    } else if (errno != ENOENT) {
        fail(path);
    }

    const QByteArray temporary = QFile::encodeName(temporaryName());
    // An existing document's temporary file stays inaccessible to others until
    // its original mode is restored. A new file retains the previous 0666/umask
    // behavior. Metadata is read and applied only through verified descriptors.
    const mode_t temporaryMode = preservePermissions ? 0000 : 0666;
    const int descriptor = ::openat(parent.get(), temporary.constData(),
                                    O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, temporaryMode);
    if (descriptor < 0) fail(path);
    Descriptor output(descriptor);
    testStage(ProjectWriterTestStage::BeforeWrite);
    qint64 offset = 0;
    while (offset < data.size()) {
        const ssize_t written = ::write(output.get(), data.constData() + offset, data.size() - offset);
        if (written <= 0) {
            ::unlinkat(parent.get(), temporary.constData(), 0);
            fail(path);
        }
        offset += written;
    }
    if ((preservePermissions && ::fchmod(output.get(), permissions) != 0) || ::fsync(output.get()) != 0 ||
        ::renameat(parent.get(), temporary.constData(), parent.get(), target.constData()) != 0) {
        ::unlinkat(parent.get(), temporary.constData(), 0);
        fail(path);
    }
}

#endif

}  // namespace

#ifdef RETROLARIUM_PROJECT_WRITER_TEST
void setProjectWriterTestHook(std::function<void(ProjectWriterTestStage)> hook) { testHook() = std::move(hook); }
#endif

// Deliberate exceptions include theme.cpp, mame_config.cpp, emulator_runner.cpp,
// audio_player.cpp, project_lock.cpp, autotest.cpp, tas_editor.cpp, app/main.cpp
// and shell/main_window.cpp. They write application data, emulator state or new
// projects in their own locations, not documents in an existing bounded project.
// Project documents, build/playtest records and project sound/music models use
// this bounded writer instead.
bool writeFileWithin(const QString& boundary, const QString& path, const QByteArray& data, QString* error) {
    try {
        if (!QDir::isAbsolutePath(boundary) || !QDir::isAbsolutePath(path)) fail(path);
        const QString cleanBoundary = QDir::cleanPath(QDir::fromNativeSeparators(boundary));
        const QString cleanPath = QDir::fromNativeSeparators(path);
        if (QDir::cleanPath(cleanPath) != cleanPath || !within(cleanPath, cleanBoundary)) fail(path);
        const QString relative = QDir(cleanBoundary).relativeFilePath(cleanPath);
        if (relative.isEmpty() || relative == QLatin1String(".") || relative.startsWith(QStringLiteral("../"))) {
            fail(path);
        }
        writeNative(cleanBoundary, cleanPath, relative, data);
        return true;
    } catch (const std::exception& exception) {
        if (error) *error = QString::fromUtf8(exception.what());
        return false;
    }
}

}  // namespace rl::sdk
