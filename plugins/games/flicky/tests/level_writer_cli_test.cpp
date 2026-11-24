// Ordinary CLI coverage: relative and absolute staged-source arguments both
// update a small synthetic document in a temporary directory.
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStringList>
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

QJsonArray repeated(int count, const QJsonValue& value) {
    QJsonArray result;
    for (int index = 0; index < count; ++index) result.append(value);
    return result;
}

QJsonArray collisionRows() { return repeated(28, repeated(32, 0)); }

QJsonObject levelDocument() {
    QJsonArray layouts;
    const QStringList groups{QStringLiteral("background_group_3"), QStringLiteral("background_group_4"),
                             QStringLiteral("background_group_5"), QStringLiteral("chicks_a"),
                             QStringLiteral("chicks_b"),           QStringLiteral("flag_7"),
                             QStringLiteral("flag_7_6"),           QStringLiteral("flag_5")};
    for (int id = 0; id < 36; ++id) {
        QJsonObject layout{{QStringLiteral("id"), id},
                           {QStringLiteral("player"), QJsonArray{0, 0}},
                           {QStringLiteral("entry_arrow"), QJsonArray{0, 0}},
                           {QStringLiteral("cat_door"), QJsonArray{0, 0}},
                           {QStringLiteral("exit_door"), QJsonArray{0, 0}},
                           {QStringLiteral("spawners"), repeated(6, QJsonArray{0, 0})},
                           {QStringLiteral("collision"), collisionRows()},
                           {QStringLiteral("baseline_collision_stream"), QJsonArray{0}}};
        for (const auto& group : groups) layout.insert(group, QJsonArray{});
        layouts.append(layout);
    }
    return QJsonObject{
        {QStringLiteral("schema_version"), 1},
        {QStringLiteral("profile"), QStringLiteral("canonical")},
        {QStringLiteral("map"), QJsonObject{{QStringLiteral("width"), 32}, {QStringLiteral("height"), 28}}},
        {QStringLiteral("round_layouts"), repeated(48, 0)},
        {QStringLiteral("round_special_layouts"), repeated(48, 0)},
        {QStringLiteral("layouts"), layouts},
        {QStringLiteral("capacities"),
         QJsonObject{{QStringLiteral("level_data_bytes"), 1000000}, {QStringLiteral("special_tiles_bytes"), 1000000}}}};
}

bool writeFile(const QString& path, const QByteArray& bytes) {
    QFile output(path);
    return output.open(QIODevice::WriteOnly | QIODevice::Truncate) && output.write(bytes) == bytes.size();
}

QByteArray readAll(const QString& path) {
    QFile input(path);
    if (!input.open(QIODevice::ReadOnly)) return {};
    return input.readAll();
}

QString makeFixture(const QString& root) {
    expect(QDir().mkpath(root + QStringLiteral("/staged/data")), "create the staged source fixture");
    const QString levels = root + QStringLiteral("/levels.json");
    expect(writeFile(levels, QJsonDocument(levelDocument()).toJson(QJsonDocument::Compact)), "write levels.json");
    expect(
        writeFile(root + QStringLiteral("/staged/data/tables.s"), "Level_DataPointers:\nOLD_TABLES\nUnused_Block2:\n"),
        "write the tables source fixture");
    expect(writeFile(root + QStringLiteral("/staged/data/level_layout.s"),
                     "Level_SpecialTilePointers:\nOLD_SPECIAL\nTiger_JumpArcTable:\n"),
           "write the special source fixture");
    return levels;
}

void runCase(const QString& writer, const QString& root, const QString& stagedArgument, const char* label) {
    const QString levels = makeFixture(root);
    QProcess process;
    process.setWorkingDirectory(root);
    process.start(writer, {levels, stagedArgument});
    const bool finished = process.waitForFinished(30000);
    if (!finished || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        std::cerr << label << ": " << process.readAllStandardError().constData() << '\n';
        expect(false, "the level writer CLI accepts the staged-source argument");
        return;
    }

    const QString staged = root + QStringLiteral("/staged");
    const QByteArray tables = readAll(staged + QStringLiteral("/data/tables.s"));
    const QByteArray special = readAll(staged + QStringLiteral("/data/level_layout.s"));
    expect(
        tables.contains("Level_DataPointers:") && tables.contains("Unused_Block2:") && !tables.contains("OLD_TABLES"),
        label);
    expect(special.contains("Level_SpecialTilePointers:") && special.contains("Tiger_JumpArcTable:") &&
               !special.contains("OLD_SPECIAL"),
           label);
}

}  // namespace

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    if (argc != 2) {
        std::cerr << "usage: flicky_level_writer_cli_test path-to-level-writer\n";
        return 2;
    }
    QTemporaryDir folder;
    if (!folder.isValid()) return 1;
    const QString writer = QString::fromLocal8Bit(argv[1]);
    runCase(writer, folder.path() + QStringLiteral("/relative"), QStringLiteral("staged"),
            "relative staged-source writes both sources");
    const QString absoluteRoot = folder.path() + QStringLiteral("/absolute");
    runCase(writer, absoluteRoot, QDir(absoluteRoot + QStringLiteral("/staged")).absolutePath(),
            "absolute staged-source writes both sources");
    if (failures) return 1;
    std::cout << "flicky level writer: relative and absolute staged-source arguments work\n";
    return 0;
}
