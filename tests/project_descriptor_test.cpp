// Game descriptors carry metadata for Kit; projects carry editable documents.
// Both use the TOML reader, but only projects validate editable paths on load.
#include "sdk/project_file.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <iostream>

namespace {
int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void writeFile(const QString& path, const QString& text) {
    QFile file(path);
    const QByteArray bytes = text.toUtf8();
    expect(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(), "the fixture is written");
}
}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir folder;
    if (!folder.isValid()) return 1;
    const QString path = folder.filePath(QStringLiteral("game.rlj"));
    const QString descriptor = QStringLiteral(
        "schema = 'retrolarium.game'\nschema_version = 1\n"
        "[game]\nid = 'example'\nname = 'Example'\n"
        "[source]\nrom = '../roms/example.nes'\n");
    writeFile(path, descriptor);
    QString error;
    const auto metadata = rl::sdk::ProjectFile::load(path, &error);
    expect(metadata.isValid() && error.isEmpty(), "descriptor metadata loads without editable-path validation");
    expect(metadata.string(QStringLiteral("game.name")) == QLatin1String("Example"), "the game name is readable");
    expect(metadata.string(QStringLiteral("source.rom")) == QLatin1String("../roms/example.nes"),
           "descriptor metadata preserves its path text");

    const QString document = QStringLiteral("[[workspace.tabs]]\ndocument = '../documents/levels.json'\n");
    error.clear();
    const auto described = rl::sdk::ProjectFile::parse(descriptor + document, path, &error);
    expect(described.isValid() && error.isEmpty(), "descriptor document metadata is not treated as editable files");

    QString project = descriptor + document;
    project.replace(QStringLiteral("retrolarium.game"), QStringLiteral("retrolarium.project"));
    writeFile(path, project);
    error.clear();
    expect(!rl::sdk::ProjectFile::load(path, &error).isValid() && !error.isEmpty(),
           "a project with the same paths still fails validation");

    writeFile(path, QStringLiteral("schema = 'retrolarium.game'\nname = 'first'\nname = 'second'\n"));
    error.clear();
    expect(!rl::sdk::ProjectFile::load(path, &error).isValid() && !error.isEmpty(),
           "descriptor syntax is still checked");

    error.clear();
    const QString ordinary = QStringLiteral(
        "schema = 'retrolarium.project'\n[source]\nrom = 'example.nes'\n"
        "[[workspace.tabs]]\ndocument = 'documents/levels.json'\n");
    expect(rl::sdk::ProjectFile::parse(ordinary, path, &error).isValid() && error.isEmpty(),
           "ordinary projects still load");
    if (failures != 0) return 1;
    std::cout << "descriptor metadata and editable projects load separately\n";
    return 0;
}
