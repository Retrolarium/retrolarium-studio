#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

#include <functional>

namespace rl::setup {

// One file of the payload: its path inside the installation ("kit/rltool.py")
// and where its zlib-compressed bytes sit in the setup program.
struct PayloadEntry {
    QString path;
    quint64 offset = 0;
    quint64 size = 0;
    quint64 packedSize = 0;
    QByteArray sha256;
};

struct PayloadManifest {
    QString version;
    quint64 stubSize = 0;
    QList<PayloadEntry> files;
};

// The setup program is the installer itself (the stub) followed by the
// compressed files, a JSON manifest and a 32-byte footer that locates it.
// Every file is checked against its SHA-256 when it is unpacked.
class InstallerPackage final {
public:
    using Progress = std::function<void(quint64 completed, quint64 total, const QString& path)>;

    static bool read(const QString& packagePath, PayloadManifest* manifest, QString* error);
    // Appends every file under `payloadDirectory` to a copy of `stubPath`.
    static bool create(const QString& stubPath, const QString& payloadDirectory, const QString& outputPath,
                       const QString& version, QString* error);
    static bool verify(const QString& packagePath, QString* error);
    static bool extract(const QString& packagePath, const PayloadManifest& manifest, const QString& destination,
                        const Progress& progress, QString* error);
    // The installer alone, kept as the installation's Uninstall.exe.
    static bool copyStub(const QString& packagePath, quint64 stubSize, const QString& destination, QString* error);
};

}  // namespace rl::setup
