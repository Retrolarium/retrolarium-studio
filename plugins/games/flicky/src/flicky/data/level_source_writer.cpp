#include "flicky/data/level_source_writer.hpp"

#include "flicky/data/level_document.hpp"

#include <QFile>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStringList>

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace rl::flicky {
namespace {

QString hexByte(std::uint8_t value)
{
    return QStringLiteral("$%1").arg(value, 2, 16, QLatin1Char('0')).toUpper();
}

QString hexWord(std::uint16_t value)
{
    return QStringLiteral("$%1").arg(value, 4, 16, QLatin1Char('0')).toUpper();
}

void appendValues(QStringList &lines, const QString &label,
                  const std::vector<std::uint8_t> &bytes, bool words)
{
    const std::size_t chunkSize = words ? 20 : 10;
    if (words && bytes.size() % 2 != 0)
        throw FlickyLevelDocumentError("special-tile block is not word aligned");
    for (std::size_t offset = 0; offset < bytes.size(); offset += chunkSize) {
        const auto end = std::min(offset + chunkSize, bytes.size());
        QStringList values;
        if (words) {
            for (auto index = offset; index < end; index += 2)
                values.push_back(hexWord(static_cast<std::uint16_t>(
                    (bytes[index] << 8) | bytes[index + 1])));
        } else {
            for (auto index = offset; index < end; ++index)
                values.push_back(hexByte(bytes[index]));
        }
        const QString prefix = offset == 0
            ? (label + QLatin1Char(':')).leftJustified(16)
            : QString(16, QLatin1Char(' '));
        lines.push_back(prefix + (words ? QStringLiteral("dc.w")
                                      : QStringLiteral("dc.b")).leftJustified(7)
                        + QLatin1Char(' ') + values.join(QStringLiteral(", ")));
    }
}

QString levelSection(const FlickyLevelDocument &document)
{
    QStringList lines;
    lines.push_back(QStringLiteral("Level_DataPointers:").leftJustified(24)
                    + QStringLiteral("dc.w    Level_Data%1-Sys_GameEntryPoint")
                          .arg(document.layoutForRound(0)));
    for (int round = 1; round < 48; ++round)
        lines.push_back(QString(16, QLatin1Char(' '))
                        + QStringLiteral("dc.w    Level_Data%1-Sys_GameEntryPoint")
                              .arg(document.layoutForRound(round)));
    for (int layout = 0; layout < 36; ++layout)
        appendValues(lines, QStringLiteral("Level_Data%1").arg(layout),
                     document.encodedLevel(layout), false);
    return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

QString specialSection(const FlickyLevelDocument &document)
{
    QStringList lines;
    lines.push_back(QStringLiteral("Level_SpecialTilePointers:").leftJustified(32)
                    + QStringLiteral("dc.w    Level_SpecialTiles%1-Sys_GameEntryPoint")
                          .arg(document.objectLayoutForRound(0, QStringLiteral("flag_7"))));
    for (int round = 1; round < 48; ++round)
        lines.push_back(QString(16, QLatin1Char(' '))
                        + QStringLiteral("dc.w    Level_SpecialTiles%1-Sys_GameEntryPoint")
                              .arg(document.objectLayoutForRound(
                                  round, QStringLiteral("flag_7"))));
    for (int layout = 0; layout < 36; ++layout)
        appendValues(lines, QStringLiteral("Level_SpecialTiles%1").arg(layout),
                     document.encodedSpecial(layout), true);
    const auto usage = document.capacityUsage();
    const auto capacity = document.capacityLimits();
    const int padding = capacity.second - usage.second;
    if (padding < 0 || padding % 2 != 0)
        throw FlickyLevelDocumentError("special-tile padding is invalid");
    if (padding)
        lines.push_back(QString(16, QLatin1Char(' '))
                        + QStringLiteral("dc.w    [%1]$FFFF").arg(padding / 2));
    return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

QByteArray replacedRange(const QString &path, const QByteArray &start,
                         const QByteArray &end, const QString &replacement)
{
    QFile input(path);
    if (!input.open(QIODevice::ReadOnly))
        throw FlickyLevelDocumentError("cannot read staged Flicky source: "
                                       + path.toStdString());
    QByteArray bytes = input.readAll();
    input.close();
    const int begin = bytes.indexOf(start);
    const int finish = bytes.indexOf(end, begin + start.size());
    if (begin < 0 || finish < 0 || bytes.indexOf(start, begin + start.size()) >= 0
        || bytes.indexOf(end, finish + end.size()) >= 0)
        throw FlickyLevelDocumentError("staged Flicky source range is missing or ambiguous: "
                                       + path.toStdString());
    // Source scripts use universal-newline text I/O before rendering these
    // sections. Match that behavior even on a Windows checkout.
    bytes.replace("\r\n", "\n");
    const int normalizedBegin = bytes.indexOf(start);
    const int normalizedFinish = bytes.indexOf(end, normalizedBegin + start.size());
    bytes.replace(normalizedBegin, normalizedFinish - normalizedBegin,
                  replacement.toUtf8());
    return bytes;
}

void writeStagedFile(const QString &path, const QByteArray &bytes)
{
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly))
        throw FlickyLevelDocumentError("cannot open staged Flicky source: "
            + path.toStdString() + ": " + output.errorString().toStdString());
    if (output.write(bytes) != bytes.size())
        throw FlickyLevelDocumentError("cannot write staged Flicky source: "
            + path.toStdString() + ": " + output.errorString().toStdString());
    if (!output.commit())
        throw FlickyLevelDocumentError("cannot commit staged Flicky source: "
            + path.toStdString() + ": " + output.errorString().toStdString());
}

QByteArray bonusPlacementSource(const FlickyLevelDocument &document,
                                const QString &path)
{
    QFile input(path);
    if (!input.open(QIODevice::ReadOnly))
        throw FlickyLevelDocumentError("cannot read staged Flicky bonus source: "
                                       + path.toStdString());
    QString source = QString::fromUtf8(input.readAll());
    struct Group { const char *id; const char *start; const char *end; };
    const Group groups[] = {
        {"seesaw", "Obj_BonusSeesaw:", "Obj_BonusTiger:"},
        {"tiger", "Obj_BonusTiger:", "Obj_BonusHeldChick:"},
        {"chick_spawn", "BonusChick_StateWait:", "BonusChick_StateFly:"},
    };
    const QRegularExpression xWord(
        QStringLiteral(R"(move\.w\s+#\$([0-9A-Fa-f]+),\$30\(a0\))"));
    const QRegularExpression yWord(
        QStringLiteral(R"(move\.w\s+#\$([0-9A-Fa-f]+),\$24\(a0\))"));
    for (const auto &group : groups) {
        const QString start = QString::fromLatin1(group.start);
        const QString end = QString::fromLatin1(group.end);
        const int begin = source.indexOf(start);
        const int finish = source.indexOf(end);
        if (begin < 0 || finish <= begin
            || source.indexOf(start, begin + start.size()) >= 0
            || source.indexOf(end, finish + end.size()) >= 0)
            throw FlickyLevelDocumentError("ambiguous staged Flicky bonus source: "
                                           + std::string(group.id));
        QString block = source.mid(begin, finish - begin);
        const auto record = document.bonusPlacement(QString::fromLatin1(group.id));
        const auto baselineXs = record.value(QStringLiteral("baseline_x")).toArray();
        const auto currentXs = record.value(QStringLiteral("x")).toArray();
        struct Patch { int start; int length; QString value; };
        std::vector<Patch> patches;
        auto xMatches = xWord.globalMatch(block);
        int count = 0;
        while (xMatches.hasNext()) {
            const auto match = xMatches.next();
            bool okay = false;
            const int actual = match.captured(1).toInt(&okay, 16);
            if (count >= 2 || !okay || actual != baselineXs.at(count).toInt())
                throw FlickyLevelDocumentError("staged Flicky bonus X differs from baseline: "
                                               + std::string(group.id));
            patches.push_back({match.capturedStart(1), match.capturedLength(1),
                QString::number(currentXs.at(count).toInt(), 16).toUpper()});
            ++count;
        }
        auto yMatches = yWord.globalMatch(block);
        int yCount = 0;
        while (yMatches.hasNext()) {
            const auto match = yMatches.next();
            bool okay = false;
            const int actual = match.captured(1).toInt(&okay, 16);
            if (!okay || actual != record.value(QStringLiteral("baseline_y")).toInt())
                throw FlickyLevelDocumentError("staged Flicky bonus Y differs from baseline: "
                                               + std::string(group.id));
            patches.push_back({match.capturedStart(1), match.capturedLength(1),
                QString::number(record.value(QStringLiteral("y")).toInt(), 16).toUpper()});
            ++yCount;
        }
        if (count != 2 || yCount != 1)
            throw FlickyLevelDocumentError("staged Flicky bonus placement is incomplete: "
                                           + std::string(group.id));
        std::sort(patches.begin(), patches.end(), [](const Patch &a, const Patch &b) {
            return a.start > b.start;
        });
        for (const auto &patch : patches)
            block.replace(patch.start, patch.length, patch.value);
        source.replace(begin, finish - begin, block);
    }
    return source.toUtf8();
}

} // namespace

void writeFlickyLevelSources(const FlickyLevelDocument &document,
                             const QString &stagedSource)
{
    document.validateCapacity();
    const auto tablesPath = stagedSource + QStringLiteral("/data/tables.s");
    const auto specialPath = stagedSource + QStringLiteral("/data/level_layout.s");
    // Preflight both ranges before modifying either file. A build error must
    // not leave a half-replaced staged level pair.
    const auto tables = replacedRange(tablesPath,
        "Level_DataPointers:", "Unused_Block2:", levelSection(document));
    const auto special = replacedRange(specialPath,
        "Level_SpecialTilePointers:", "Tiger_JumpArcTable:",
        specialSection(document));
    const auto bonusPath = stagedSource + QStringLiteral("/game/bonus/objects.s");
    const auto bonus = document.hasBonusPlacements()
        ? bonusPlacementSource(document, bonusPath) : QByteArray {};
    writeStagedFile(tablesPath, tables);
    writeStagedFile(specialPath, special);
    if (document.hasBonusPlacements()) writeStagedFile(bonusPath, bonus);
}

} // namespace rl::flicky
