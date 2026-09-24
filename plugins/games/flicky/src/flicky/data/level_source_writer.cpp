#include <retrolarium/flicky_level_source_writer.hpp>

#include <retrolarium/flicky_level_document.hpp>

#include <QFile>
#include <QSaveFile>
#include <QStringList>

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace retrolarium {
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
    writeStagedFile(tablesPath, tables);
    writeStagedFile(specialPath, special);
}

} // namespace retrolarium
