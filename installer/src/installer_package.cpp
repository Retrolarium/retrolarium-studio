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

// A JSON number as a size or offset; -1 for anything that is not a whole
// number a double holds exactly (converting those to quint64 is undefined).
qint64 jsonSize(const QJsonValue& value) {
    const double number = value.toDouble(-1);
    if (!(number >= 0 && number <= 9007199254740992.0) || number != static_cast<double>(static_cast<qint64>(number))) {
        return -1;
    }
    return static_cast<qint64>(number);
}

// Where the package ends: before the certificate table when the program is
// signed. Signing appends the table (8-byte aligned, zero padded first) and
// records it in the PE header's security directory.
// The file offset of the PE header's security directory entry, or -1.
qint64 securityDirectoryOffset(QFile* file) {
    if (!file->seek(0x3c)) return -1;
    const QByteArray offsetBytes = file->read(4);
    if (offsetBytes.size() != 4) return -1;
    const qint64 pe = qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(offsetBytes.constData()));
    if (!file->seek(pe)) return -1;
    const QByteArray header = file->read(4 + 20 + 2);
    if (header.size() != 26 || !header.startsWith(QByteArray("PE\0\0", 4))) return -1;
    const quint16 magic = qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(header.constData() + 24));
    // The data directories follow the optional header's fixed part; the
    // security one is the fifth.
    return pe + 24 + (magic == 0x20b ? 112 : 96) + 4 * 8;
}

qint64 packageEnd(QFile* file) {
    const qint64 size = file->size();
    const qint64 entry = securityDirectoryOffset(file);
    if (entry < 0 || !file->seek(entry)) return size;
    const QByteArray security = file->read(8);
    if (security.size() != 8) return size;
    const qint64 start = qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(security.constData()));
    const qint64 length = qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(security.constData() + 4));
    if (start == 0 || length == 0 || start + length != size) return size;
    return start;
}

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
    // A signed program pads the end to eight bytes before its certificate.
    qint64 end = packageEnd(&file);
    const int maxPadding = end == file.size() ? 0 : 7;
    QByteArray footer;
    for (int padding = 0; padding <= maxPadding && end - padding >= kFooterSize; ++padding) {
        if (!file.seek(end - padding - kFooterSize)) break;
        const QByteArray tail = file.read(kFooterSize + padding);
        if (tail.startsWith(kFooterMagic) && tail.mid(kFooterSize).count('\0') == padding) {
            footer = tail.left(kFooterSize);
            end -= padding;
            break;
        }
    }
    if (!footer.startsWith(kFooterMagic)) {
        if (error) *error = QStringLiteral("this program holds no Retrolarium Studio files");
        return false;
    }
    QDataStream stream(footer);
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
    const quint64 packageSize = static_cast<quint64>(end);
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
    const qint64 stubSize = jsonSize(root.value(QStringLiteral("stubSize")));
    parsed.stubSize = stubSize < 0 ? 0 : static_cast<quint64>(stubSize);
    if (parsed.version.isEmpty() || parsed.stubSize == 0 || parsed.stubSize > manifestOffset) {
        if (error) *error = QStringLiteral("the setup package manifest is damaged");
        return false;
    }
    for (const QJsonValue& value : root.value(QStringLiteral("files")).toArray()) {
        const QJsonObject object = value.toObject();
        PayloadEntry entry;
        entry.path = object.value(QStringLiteral("path")).toString();
        const qint64 offset = jsonSize(object.value(QStringLiteral("offset")));
        const qint64 size = jsonSize(object.value(QStringLiteral("size")));
        const qint64 packedSize = jsonSize(object.value(QStringLiteral("packedSize")));
        entry.offset = offset < 0 ? 0 : static_cast<quint64>(offset);
        entry.size = size < 0 ? 0 : static_cast<quint64>(size);
        entry.packedSize = packedSize < 0 ? 0 : static_cast<quint64>(packedSize);
        entry.sha256 = QByteArray::fromHex(object.value(QStringLiteral("sha256")).toString().toLatin1());
        // Subtracting rather than adding: offsets are below 2^53, but checked
        // this way no sum can wrap.
        if (!safePath(entry.path) || offset < 0 || size < 0 || entry.packedSize < 4 || entry.sha256.size() != 32 ||
            entry.offset < parsed.stubSize || entry.packedSize > manifestOffset ||
            entry.offset > manifestOffset - entry.packedSize || entry.size > kMaxFileSize) {
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
        if (!safePath(path) || !payload.open(QIODevice::ReadOnly))
            return fail(QStringLiteral("cannot pack %1").arg(path));
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
    // A signed package's header points at its certificate, past the stub's
    // end: the copy drops that pointer, or Windows would find a damaged
    // signature. (The first chunk holds the whole header.)
    const qint64 securityEntry = securityDirectoryOffset(&source);
    if (!source.seek(0)) {
        if (error) *error = QStringLiteral("could not copy the uninstaller");
        return false;
    }
    quint64 remaining = stubSize;
    qint64 position = 0;
    while (remaining > 0) {
        QByteArray chunk = source.read(static_cast<qint64>(qMin<quint64>(remaining, kCopyChunk)));
        if (securityEntry >= position && securityEntry + 8 <= position + chunk.size()) {
            chunk.replace(static_cast<int>(securityEntry - position), 8, QByteArray(8, '\0'));
        }
        if (chunk.isEmpty() || output.write(chunk) != chunk.size()) {
            output.cancelWriting();
            if (error) *error = QStringLiteral("could not copy the uninstaller");
            return false;
        }
        position += chunk.size();
        remaining -= static_cast<quint64>(chunk.size());
    }
    if (!output.commit()) {
        if (error) *error = output.errorString();
        return false;
    }
    return true;
}

}  // namespace rl::setup
