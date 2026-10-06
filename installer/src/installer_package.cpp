#include "installer_package.hpp"

#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QtEndian>

namespace rl::setup {

namespace {

constexpr int kFooterSize = 32;
const QByteArray kFooterMagic("RETROLARIUM-PKG1", 16);
constexpr int kFormat = 1;
constexpr qint64 kCopyChunk = 256 * 1024;
constexpr quint64 kMaxFileSize = 512ULL * 1024 * 1024;
constexpr quint64 kMaxManifestSize = 16ULL * 1024 * 1024;

// A relative path that stays inside the installation directory.
bool safePath(const QString& path) {
    if (path.isEmpty() || path.startsWith(QLatin1Char('/')) || path.contains(QLatin1Char('\\')) ||
        path.contains(QLatin1Char(':'))) {
        return false;
    }
    for (const QString& part : path.split(QLatin1Char('/'))) {
        if (part.isEmpty() || part == QLatin1String(".") || part == QLatin1String("..")) return false;
    }
    return true;
}

bool unpackEntry(QFile* source, const PayloadEntry& entry, QByteArray* plain, QString* error) {
    if (!source->seek(static_cast<qint64>(entry.offset))) {
        if (error) *error = QStringLiteral("could not seek to %1").arg(entry.path);
        return false;
    }
    const QByteArray packed = source->read(static_cast<qint64>(entry.packedSize));
    if (static_cast<quint64>(packed.size()) != entry.packedSize || packed.size() < 4) {
        if (error) *error = QStringLiteral("packaged file %1 is truncated").arg(entry.path);
        return false;
    }
    // qCompress stores the uncompressed size in a big-endian prefix: check it
    // before qUncompress allocates from an untrusted value.
    const quint32 advertised = qFromBigEndian<quint32>(reinterpret_cast<const uchar*>(packed.constData()));
    if (advertised != entry.size) {
        if (error) *error = QStringLiteral("invalid compressed size for %1").arg(entry.path);
        return false;
    }
    const QByteArray unpacked = entry.size == 0 ? QByteArray() : qUncompress(packed);
    if (static_cast<quint64>(unpacked.size()) != entry.size ||
        QCryptographicHash::hash(unpacked, QCryptographicHash::Sha256) != entry.sha256) {
        if (error) *error = QStringLiteral("checksum verification failed for %1").arg(entry.path);
        return false;
    }
    if (plain) *plain = unpacked;
    return true;
}

}  // namespace

bool InstallerPackage::read(const QString& packagePath, PayloadManifest* manifest, QString* error) {
    QFile file(packagePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("could not open the setup package: %1").arg(file.errorString());
        return false;
    }
    if (file.size() < kFooterSize || !file.seek(file.size() - kFooterSize)) {
        if (error) *error = QStringLiteral("this program holds no Retrolarium Studio files");
        return false;
    }
    QDataStream stream(file.read(kFooterSize));
    stream.setByteOrder(QDataStream::LittleEndian);
    char magic[16] = {};
    quint64 manifestOffset = 0;
    quint64 manifestSize = 0;
    stream.readRawData(magic, sizeof(magic));
    stream >> manifestOffset >> manifestSize;
    if (QByteArray(magic, sizeof(magic)) != kFooterMagic || stream.status() != QDataStream::Ok) {
        if (error) *error = QStringLiteral("this program holds no Retrolarium Studio files");
        return false;
    }
    const quint64 packageSize = static_cast<quint64>(file.size());
    if (manifestSize == 0 || manifestSize > kMaxManifestSize || manifestOffset >= packageSize ||
        manifestOffset + manifestSize + kFooterSize != packageSize || !file.seek(static_cast<qint64>(manifestOffset))) {
        if (error) *error = QStringLiteral("the setup package manifest is damaged");
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.read(static_cast<qint64>(manifestSize)), &parseError);
    const QJsonObject root = document.object();
    if (parseError.error != QJsonParseError::NoError || root.value(QStringLiteral("format")).toInt() != kFormat) {
        if (error) *error = QStringLiteral("the setup package manifest is damaged");
        return false;
    }
    PayloadManifest parsed;
    parsed.version = root.value(QStringLiteral("version")).toString();
    parsed.stubSize = static_cast<quint64>(root.value(QStringLiteral("stubSize")).toDouble());
    if (parsed.version.isEmpty() || parsed.stubSize == 0 || parsed.stubSize > manifestOffset) {
        if (error) *error = QStringLiteral("the setup package manifest is damaged");
        return false;
    }
    for (const QJsonValue& value : root.value(QStringLiteral("files")).toArray()) {
        const QJsonObject object = value.toObject();
        PayloadEntry entry;
        entry.path = object.value(QStringLiteral("path")).toString();
        entry.offset = static_cast<quint64>(object.value(QStringLiteral("offset")).toDouble());
        entry.size = static_cast<quint64>(object.value(QStringLiteral("size")).toDouble());
        entry.packedSize = static_cast<quint64>(object.value(QStringLiteral("packedSize")).toDouble());
        entry.sha256 = QByteArray::fromHex(object.value(QStringLiteral("sha256")).toString().toLatin1());
        if (!safePath(entry.path) || entry.packedSize < 4 || entry.sha256.size() != 32 ||
            entry.offset < parsed.stubSize || entry.offset + entry.packedSize > manifestOffset ||
            entry.size > kMaxFileSize) {
            if (error) *error = QStringLiteral("invalid packaged file: %1").arg(entry.path);
            return false;
        }
        parsed.files.append(entry);
    }
    if (parsed.files.isEmpty()) {
        if (error) *error = QStringLiteral("the setup package holds no files");
        return false;
    }
    if (manifest) *manifest = parsed;
    return true;
}

bool InstallerPackage::create(const QString& stubPath, const QString& payloadDirectory, const QString& outputPath,
                              const QString& version, QString* error) {
    QFile stub(stubPath);
    if (!stub.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("could not open the installer: %1").arg(stub.errorString());
        return false;
    }
    QSaveFile output(outputPath);
    if (!output.open(QIODevice::WriteOnly)) {
        if (error) *error = QStringLiteral("could not create %1: %2").arg(outputPath, output.errorString());
        return false;
    }
    auto fail = [&](const QString& message) {
        if (error) *error = message;
        output.cancelWriting();
        return false;
    };
    while (!stub.atEnd()) {
        const QByteArray chunk = stub.read(kCopyChunk);
        if (output.write(chunk) != chunk.size()) return fail(output.errorString());
    }
    const quint64 stubSize = static_cast<quint64>(output.pos());

    const QDir root(payloadDirectory);
    QStringList paths;
    QDirIterator it(payloadDirectory, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) paths << root.relativeFilePath(it.next());
    paths.sort();
    if (!paths.contains(QStringLiteral("retrolarium-studio.exe"))) {
        return fail(QStringLiteral("%1 has no retrolarium-studio.exe").arg(payloadDirectory));
    }
    QJsonArray files;
    for (const QString& path : paths) {
        QFile payload(root.filePath(path));
        if (!safePath(path) || !payload.open(QIODevice::ReadOnly)) return fail(QStringLiteral("cannot pack %1").arg(path));
        const QByteArray plain = payload.readAll();
        if (static_cast<quint64>(plain.size()) > kMaxFileSize) return fail(QStringLiteral("%1 is too large").arg(path));
        const QByteArray packed = qCompress(plain, 9);
        const quint64 offset = static_cast<quint64>(output.pos());
        if (output.write(packed) != packed.size()) return fail(output.errorString());
        QJsonObject entry;
        entry.insert(QStringLiteral("path"), path);
        entry.insert(QStringLiteral("offset"), static_cast<double>(offset));
        entry.insert(QStringLiteral("size"), static_cast<double>(plain.size()));
        entry.insert(QStringLiteral("packedSize"), static_cast<double>(packed.size()));
        entry.insert(QStringLiteral("sha256"),
                     QString::fromLatin1(QCryptographicHash::hash(plain, QCryptographicHash::Sha256).toHex()));
        files.append(entry);
    }
    QJsonObject manifest;
    manifest.insert(QStringLiteral("format"), kFormat);
    manifest.insert(QStringLiteral("version"), version);
    manifest.insert(QStringLiteral("stubSize"), static_cast<double>(stubSize));
    manifest.insert(QStringLiteral("files"), files);
    const QByteArray json = QJsonDocument(manifest).toJson(QJsonDocument::Compact);
    const quint64 manifestOffset = static_cast<quint64>(output.pos());
    if (output.write(json) != json.size()) return fail(output.errorString());
    QByteArray footer;
    QDataStream footerStream(&footer, QIODevice::WriteOnly);
    footerStream.setByteOrder(QDataStream::LittleEndian);
    footerStream.writeRawData(kFooterMagic.constData(), kFooterMagic.size());
    footerStream << manifestOffset << static_cast<quint64>(json.size());
    if (footer.size() != kFooterSize || output.write(footer) != footer.size()) return fail(output.errorString());
    if (!output.commit()) {
        if (error) *error = output.errorString();
        return false;
    }
    return true;
}

bool InstallerPackage::verify(const QString& packagePath, QString* error) {
    PayloadManifest manifest;
    if (!read(packagePath, &manifest, error)) return false;
    QFile source(packagePath);
    if (!source.open(QIODevice::ReadOnly)) {
        if (error) *error = source.errorString();
        return false;
    }
    for (const PayloadEntry& entry : manifest.files) {
        if (!unpackEntry(&source, entry, nullptr, error)) return false;
    }
    return true;
}

bool InstallerPackage::extract(const QString& packagePath, const PayloadManifest& manifest, const QString& destination,
                               const Progress& progress, QString* error) {
    QFile source(packagePath);
    if (!source.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("could not open the setup package: %1").arg(source.errorString());
        return false;
    }
    quint64 total = 0;
    for (const PayloadEntry& entry : manifest.files) total += entry.size;
    quint64 completed = 0;
    const QDir root(destination);
    for (const PayloadEntry& entry : manifest.files) {
        QByteArray plain;
        if (!unpackEntry(&source, entry, &plain, error)) return false;
        const QString path = root.filePath(entry.path);
        QDir().mkpath(QFileInfo(path).absolutePath());
        QSaveFile output(path);
        if (!output.open(QIODevice::WriteOnly) || output.write(plain) != plain.size() || !output.commit()) {
            if (error) *error = QStringLiteral("could not write %1: %2").arg(entry.path, output.errorString());
            return false;
        }
        completed += entry.size;
        if (progress) progress(completed, total, entry.path);
    }
    return true;
}

bool InstallerPackage::copyStub(const QString& packagePath, quint64 stubSize, const QString& destination,
                                QString* error) {
    QFile source(packagePath);
    QSaveFile output(destination);
    if (!source.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly)) {
        if (error) *error = QStringLiteral("could not copy the uninstaller");
        return false;
    }
    quint64 remaining = stubSize;
    while (remaining > 0) {
        const QByteArray chunk = source.read(static_cast<qint64>(qMin<quint64>(remaining, kCopyChunk)));
        if (chunk.isEmpty() || output.write(chunk) != chunk.size()) {
            output.cancelWriting();
            if (error) *error = QStringLiteral("could not copy the uninstaller");
            return false;
        }
        remaining -= static_cast<quint64>(chunk.size());
    }
    if (!output.commit()) {
        if (error) *error = output.errorString();
        return false;
    }
    return true;
}

}  // namespace rl::setup
