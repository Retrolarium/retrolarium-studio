// The setup program's payload: a package made by InstallerPackage::create
// reads, verifies and unpacks to the same files; a truncated package, sizes
// past the file or past the limits, paths that leave the installation or
// name Windows devices, and damaged file data are refused. Install,
// uninstall and their rollback write the registry and the user's shell
// folders, which this test cannot redirect; they are checked by hand.
//   installer_package_test

#include "installer_package.hpp"
#include "user_data_paths.hpp"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <functional>
#include <iostream>

using rl::setup::InstallerPackage;
using rl::setup::PayloadManifest;

namespace {

int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

bool writeFile(const QString& path, const QByteArray& data) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}

QByteArray readFile(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

// A stub without a PE header: "MZ" and zeros past the header offset.
QByteArray stub() { return QByteArray("MZ") + QByteArray(0x3e, '\0'); }

struct Item {
    QString path;
    QByteArray data;
};

// A package written by hand, as create() lays it out; `tamper` changes the
// manifest before it is written.
QByteArray handMade(const QList<Item>& items, const std::function<void(QJsonObject&)>& tamper = {}) {
    QByteArray out = stub();
    const double stubSize = out.size();
    QJsonArray files;
    for (const Item& item : items) {
        const QByteArray packed = qCompress(item.data, 9);
        QJsonObject entry;
        entry.insert(QStringLiteral("path"), item.path);
        entry.insert(QStringLiteral("offset"), static_cast<double>(out.size()));
        entry.insert(QStringLiteral("size"), static_cast<double>(item.data.size()));
        entry.insert(QStringLiteral("packedSize"), static_cast<double>(packed.size()));
        entry.insert(QStringLiteral("sha256"),
                     QString::fromLatin1(QCryptographicHash::hash(item.data, QCryptographicHash::Sha256).toHex()));
        files.append(entry);
        out += packed;
    }
    QJsonObject manifest{{QStringLiteral("format"), 1},
                         {QStringLiteral("version"), QStringLiteral("0.2.0")},
                         {QStringLiteral("stubSize"), stubSize},
                         {QStringLiteral("files"), files}};
    if (tamper) tamper(manifest);
    const QByteArray json = QJsonDocument(manifest).toJson(QJsonDocument::Compact);
    const quint64 offset = static_cast<quint64>(out.size());
    out += json;
    QByteArray footer;
    QDataStream stream(&footer, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.writeRawData("RETROLARIUM-PKG1", 16);
    stream << offset << static_cast<quint64>(json.size());
    out += footer;
    return out;
}

// Changes the first file's entry of a manifest.
std::function<void(QJsonObject&)> firstEntry(const QString& key, const QJsonValue& value) {
    return [key, value](QJsonObject& manifest) {
        QJsonArray files = manifest.value(QStringLiteral("files")).toArray();
        QJsonObject entry = files.at(0).toObject();
        entry.insert(key, value);
        files.replace(0, entry);
        manifest.insert(QStringLiteral("files"), files);
    };
}

bool reads(const QTemporaryDir& dir, const QByteArray& package, QString* error = nullptr) {
    const QString path = dir.filePath(QStringLiteral("probe.exe"));
    if (!writeFile(path, package)) return false;
    PayloadManifest manifest;
    QString problem;
    const bool ok = InstallerPackage::read(path, &manifest, &problem);
    if (error) *error = problem;
    return ok;
}

}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir dataPaths;
    const QString documents = dataPaths.path() + QStringLiteral("/Documents");
    const QString local = dataPaths.path() + QStringLiteral("/Local");
    expect(rl::setup::userDataPaths(false, documents, local).isEmpty(), "user data is opt-in");
    const QStringList selected = rl::setup::userDataPaths(true, documents, local);
    expect(selected.contains(local + QStringLiteral("/Retrolarium/Retrolarium Studio")),
           "saves and movies are selected");
    expect(selected.contains(local + QStringLiteral("/Retrolarium/Kit/verified")), "verification records are selected");
    expect(selected.contains(local + QStringLiteral("/Retrolarium/settings.json")), "kit settings are selected");
    expect(!selected.contains(local + QStringLiteral("/Retrolarium")), "unrelated local data is kept");
    expect(rl::setup::localUserDataDirectory(QString()).isEmpty() &&
               rl::setup::localUserDataDirectory(QStringLiteral("relative")).isEmpty() &&
               rl::setup::localUserDataDirectory(QDir::rootPath()).isEmpty(),
           "missing, relative and root local folders are not selected");
    expect(rl::setup::userDataPaths(true, QString(), QString()).isEmpty(),
           "missing shell folders never become relative paths");
    QTemporaryDir dir;
    if (!dir.isValid()) {
        std::cerr << "FAIL: no temporary folder\n";
        return 1;
    }

    // A package as packaging makes it: read, verified and unpacked unchanged.
    const QString payload = dir.filePath(QStringLiteral("payload"));
    expect(writeFile(QDir(payload).filePath(QStringLiteral("retrolarium-studio.exe")), "studio") &&
               writeFile(QDir(payload).filePath(QStringLiteral("kit/rltool.py")), "print('kit')\n") &&
               writeFile(QDir(payload).filePath(QStringLiteral("mame/empty.txt")), QByteArray()),
           "the payload is written");
    const QString stubPath = dir.filePath(QStringLiteral("stub.exe"));
    expect(writeFile(stubPath, stub()), "the stub is written");
    const QString setup = dir.filePath(QStringLiteral("setup.exe"));
    QString error;
    expect(InstallerPackage::create(stubPath, payload, setup, QStringLiteral("0.2.0"), &error), "a package is made");
    PayloadManifest manifest;
    expect(InstallerPackage::read(setup, &manifest, &error), "the package reads");
    expect(manifest.version == QLatin1String("0.2.0") && manifest.files.size() == 3, "its manifest names every file");
    expect(manifest.stubSize == static_cast<quint64>(stub().size()), "the stub's size is recorded");
    expect(InstallerPackage::verify(setup, &error), "the package verifies");
    const QString installed = dir.filePath(QStringLiteral("installed"));
    quint64 reported = 0;
    expect(InstallerPackage::extract(
               setup, manifest, installed,
               [&reported](quint64 completed, quint64, const QString&) { reported = completed; }, &error),
           "the package unpacks");
    expect(readFile(QDir(installed).filePath(QStringLiteral("kit/rltool.py"))) == "print('kit')\n",
           "an unpacked file holds its bytes");
    expect(QFileInfo::exists(QDir(installed).filePath(QStringLiteral("mame/empty.txt"))), "an empty file unpacks");
    expect(reported == 6 + 13, "progress counts every byte");
    const QString uninstaller = dir.filePath(QStringLiteral("Uninstall.exe"));
    expect(InstallerPackage::copyStub(setup, manifest.stubSize, uninstaller, &error) && readFile(uninstaller) == stub(),
           "the stub alone is the uninstaller");

    // Truncated: the footer is gone.
    QByteArray whole = readFile(setup);
    expect(!reads(dir, whole.left(whole.size() - 10)), "a truncated package is refused");
    expect(!reads(dir, whole.left(stub().size())), "a stub without payload is refused");

    const QList<Item> items{{QStringLiteral("retrolarium-studio.exe"), QByteArray("studio")}};
    expect(reads(dir, handMade(items)), "a package written by hand reads");

    // Sizes past the file or the limits.
    expect(!reads(dir, handMade(items, firstEntry(QStringLiteral("size"), 1e15))), "a huge file size is refused");
    expect(!reads(dir, handMade(items, firstEntry(QStringLiteral("offset"), 9007199254740992.0))),
           "an offset past the package is refused");
    expect(!reads(dir, handMade(items, firstEntry(QStringLiteral("packedSize"), 1e12))),
           "a packed size past the manifest is refused");
    expect(!reads(dir, handMade(items, firstEntry(QStringLiteral("offset"), -1))), "a negative offset is refused");
    expect(!reads(dir, handMade(items, firstEntry(QStringLiteral("size"), 1.5))), "a fractional size is refused");
    expect(!reads(dir, handMade(items, firstEntry(QStringLiteral("offset"), 2))), "a file inside the stub is refused");

    // Paths that leave the installation or name devices.
    for (const char* path :
         {"../evil.txt", "kit/../../evil.txt", "/evil.txt", "C:/evil.txt", "kit\\evil.txt", "kit//evil.txt", "CON",
          "nul.txt", "kit/Aux.py", "COM1", "lpt9.log", "kit./a.py", "kit/a.py.", "kit/a.py "}) {
        QString why;
        if (reads(dir, handMade({{QString::fromLatin1(path), QByteArray("x")}}), &why)) {
            std::cerr << "FAIL: the payload path " << path << " is accepted\n";
            ++failures;
        }
    }
    for (const char* path : {"console.txt", "kit/con-fig.py", "COM10", "lpt.txt", "nul_device/readme.md"}) {
        if (!reads(dir, handMade({{QString::fromLatin1(path), QByteArray("x")}}))) {
            std::cerr << "FAIL: the ordinary payload path " << path << " is refused\n";
            ++failures;
        }
    }

    // Damaged data: the checksum catches it when the file is unpacked.
    QByteArray damaged = handMade(items);
    damaged[stub().size() + 6] = static_cast<char>(damaged.at(stub().size() + 6) ^ 0x5a);
    const QString damagedPath = dir.filePath(QStringLiteral("damaged.exe"));
    expect(writeFile(damagedPath, damaged), "the damaged package is written");
    PayloadManifest damagedManifest;
    if (InstallerPackage::read(damagedPath, &damagedManifest, &error)) {
        expect(!InstallerPackage::verify(damagedPath, &error), "damaged file data does not verify");
        expect(!InstallerPackage::extract(damagedPath, damagedManifest, dir.filePath(QStringLiteral("broken")), {},
                                          &error),
               "damaged file data does not unpack");
    }
    // A file hash that is not a SHA-256.
    expect(!reads(dir, handMade(items, firstEntry(QStringLiteral("sha256"), QStringLiteral("00")))),
           "a hash that is not SHA-256 is refused");

    if (failures != 0) return 1;
    std::cout << "installer package: reading, checking and unpacking hold\n";
    return 0;
}
