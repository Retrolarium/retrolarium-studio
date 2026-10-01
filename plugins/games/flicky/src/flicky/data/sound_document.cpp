#include "flicky/data/sound_document.hpp"

#include <QFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>

#include <algorithm>

namespace rl::flicky {
namespace {

[[noreturn]] void fail(const QString &message)
{
    throw FlickySoundDocumentError(message.toStdString());
}

struct Token final {
    QString text;
    int start = 0;
    int length = 0;
};

struct Line final {
    QString content;
    int absolute = 0;
};

QVector<Line> blockLines(const QString &source, int start, int end)
{
    QVector<Line> result;
    int cursor = start;
    bool first = true;
    while (cursor < end) {
        int next = source.indexOf(QLatin1Char('\n'), cursor);
        if (next < 0 || next > end) next = end;
        const auto line = source.mid(cursor, next - cursor);
        const int prefix = first ? line.indexOf(QLatin1Char(':')) + 1 : 0;
        if (first && prefix == 0)
            fail(QStringLiteral("Flicky sound label has no colon"));
        auto content = line.mid(prefix);
        const int comment = content.indexOf(QLatin1Char(';'));
        if (comment >= 0) content.truncate(comment);
        if (!content.trimmed().isEmpty()) result.push_back({content, cursor + prefix});
        cursor = next + 1;
        first = false;
    }
    return result;
}

QVector<Token> commaTokens(const QString &body, int absolute)
{
    QVector<Token> result;
    int start = 0;
    while (start <= body.size()) {
        int next = body.indexOf(QLatin1Char(','), start);
        if (next < 0) next = body.size();
        const auto segment = body.mid(start, next - start);
        const auto trimmed = segment.trimmed();
        if (trimmed.isEmpty()) fail(QStringLiteral("empty Flicky sound operand"));
        const int relative = segment.indexOf(trimmed);
        result.push_back({trimmed, absolute + start + relative, trimmed.size()});
        if (next == body.size()) break;
        start = next + 1;
    }
    return result;
}

int number(const QString &token)
{
    static const QRegularExpression decimal(QStringLiteral(R"(^[0-9]+$)"));
    static const QRegularExpression hex(QStringLiteral(R"(^\$[0-9A-Fa-f]+$)"));
    bool okay = false;
    int result = -1;
    if (hex.match(token).hasMatch()) result = token.mid(1).toInt(&okay, 16);
    else if (decimal.match(token).hasMatch()) result = token.toInt(&okay, 10);
    if (!okay || result < 0 || result > 255)
        fail(QStringLiteral("invalid Flicky sound byte: ") + token);
    return result;
}

QString encodedByte(int value)
{
    if (value < 0 || value > 255) fail(QStringLiteral("Flicky sound byte outside 0..255"));
    return QStringLiteral("$%1").arg(value, 2, 16, QLatin1Char('0')).toUpper();
}

int commandOperands(int command, int next)
{
    switch (command) {
    case 0xE0: case 0xE1: case 0xE2: case 0xE6: case 0xE8:
    case 0xEC: case 0xEF: case 0xF3: case 0xF4: case 0xF5:
    case 0xFA: case 0xFB: case 0xFC: case 0xFD:
        return command == 0xEF && (next & 0x80) ? 2 : 1;
    case 0xE5: case 0xE9: case 0xED: case 0xEE: case 0xF1:
    case 0xF6: case 0xF8:
        return 2;
    case 0xEA: case 0xEB: return 3;
    case 0xE4: return 5;
    case 0xF0: case 0xF7: case 0xFE: return 4;
    case 0xFF: return (next & 7) == 7 ? 3 : 2;
    default: return 0;
    }
}

} // namespace

FlickySoundDocument FlickySoundDocument::load(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        fail(QStringLiteral("cannot open Flicky sound source: ") + path);
    return fromSource(QString::fromUtf8(file.readAll()));
}

FlickySoundDocument FlickySoundDocument::fromSource(const QString &source)
{
    FlickySoundDocument document;
    document.source_ = source;
    document.baseline_ = source;
    document.parsed_ = parse(source);
    document.baselineParsed_ = document.parsed_;
    return document;
}

FlickySoundDocument::Parsed FlickySoundDocument::parse(const QString &source)
{
    Parsed parsed;
    static const QRegularExpression labelRx(
        QStringLiteral(R"(^([A-Za-z_][A-Za-z0-9_]*):)"),
        QRegularExpression::MultilineOption);
    static const QRegularExpression macroRx(
        QStringLiteral(R"(^\s*(sfxheader|musicheader|sfxtrack|musicfm|musicpsg)\s+(.+?)\s*$)"));
    static const QRegularExpression byteRx(
        QStringLiteral(R"(^\s*dc\.b\s+(.+?)\s*$)"));
    QVector<QPair<QString, int>> labels;
    auto matches = labelRx.globalMatch(source);
    while (matches.hasNext()) {
        const auto match = matches.next();
        const auto id = match.captured(1);
        if (parsed.blocks.contains(id)) fail(QStringLiteral("duplicate Flicky sound label: ") + id);
        labels.push_back({id, match.capturedStart()});
        parsed.blocks.insert(id, {match.capturedStart(), 0});
    }
    if (labels.isEmpty()) fail(QStringLiteral("Flicky sound source has no labels"));
    for (int index = 0; index < labels.size(); ++index) {
        const auto &entry = labels[index];
        const int end = index + 1 < labels.size() ? labels[index + 1].second
                                                   : source.size();
        parsed.blocks[entry.first] = {entry.second, end - entry.second};
        HeaderRecord header;
        header.value.id = entry.first;
        bool hasHeader = false;
        int expectedTracks = -1;
        for (const auto &line : blockLines(source, entry.second, end)) {
            const auto macro = macroRx.match(line.content);
            if (!macro.hasMatch()) continue;
            const auto name = macro.captured(1);
            const auto args = commaTokens(macro.captured(2),
                line.absolute + macro.capturedStart(2));
            if (name == QStringLiteral("sfxheader")
                || name == QStringLiteral("musicheader")) {
                if (hasHeader) fail(QStringLiteral("duplicate Flicky sound header macro"));
                hasHeader = true;
                header.value.kind = name == QStringLiteral("sfxheader")
                    ? QStringLiteral("sfx") : QStringLiteral("music");
                if (args.size() != (header.value.kind == QStringLiteral("sfx") ? 3 : 5))
                    fail(QStringLiteral("invalid Flicky sound header operands"));
                header.value.voices = args[0].text;
                header.spans.insert(QStringLiteral("voices"),
                    {args[0].start, args[0].length});
                const int scaleIndex = header.value.kind == QStringLiteral("sfx") ? 1 : 3;
                header.value.scale = number(args[scaleIndex].text);
                header.spans.insert(QStringLiteral("scale"),
                    {args[scaleIndex].start, args[scaleIndex].length});
                if (header.value.kind == QStringLiteral("sfx")) {
                    expectedTracks = number(args[2].text);
                } else {
                    expectedTracks = number(args[1].text) + number(args[2].text);
                    header.value.tempo = number(args[4].text);
                    header.spans.insert(QStringLiteral("tempo"),
                        {args[4].start, args[4].length});
                }
                continue;
            }
            if (!hasHeader) continue;
            TrackRecord track;
            if (name == QStringLiteral("sfxtrack")) {
                if (args.size() != 5) fail(QStringLiteral("invalid SFX track operands"));
                track.value.type = QStringLiteral("sfx");
                for (int field = 0; field < args.size(); ++field) {
                    const QString key = QStringList {QStringLiteral("flags"),
                        QStringLiteral("channel"), QStringLiteral("sequence"),
                        QStringLiteral("transpose"), QStringLiteral("volume")}[field];
                    track.spans.insert(key, {args[field].start, args[field].length});
                }
                track.value.flags = number(args[0].text);
                track.value.channel = number(args[1].text);
                track.value.sequence = args[2].text;
                track.value.transpose = number(args[3].text);
                track.value.volume = number(args[4].text);
            } else if (name == QStringLiteral("musicfm")) {
                if (args.size() != 3) fail(QStringLiteral("invalid FM track operands"));
                track.value.type = QStringLiteral("fm");
                track.value.sequence = args[0].text;
                track.value.transpose = number(args[1].text);
                track.value.volume = number(args[2].text);
                for (int field = 0; field < args.size(); ++field) {
                    const QString key = QStringList {QStringLiteral("sequence"),
                        QStringLiteral("transpose"), QStringLiteral("volume")}[field];
                    track.spans.insert(key, {args[field].start, args[field].length});
                }
            } else if (name == QStringLiteral("musicpsg")) {
                if (args.size() != 5) fail(QStringLiteral("invalid PSG track operands"));
                track.value.type = QStringLiteral("psg");
                track.value.sequence = args[0].text;
                track.value.transpose = number(args[1].text);
                track.value.volume = number(args[2].text);
                track.value.pitchEnvelope = number(args[3].text);
                track.value.volumeEnvelope = number(args[4].text);
                for (int field = 0; field < args.size(); ++field) {
                    const QString key = QStringList {QStringLiteral("sequence"),
                        QStringLiteral("transpose"), QStringLiteral("volume"),
                        QStringLiteral("pitch_envelope"),
                        QStringLiteral("volume_envelope")}[field];
                    track.spans.insert(key, {args[field].start, args[field].length});
                }
            } else continue;
            header.value.tracks.push_back(track.value);
            header.tracks.push_back(track);
        }
        if (hasHeader) {
            if (header.tracks.size() != expectedTracks)
                fail(QStringLiteral("Flicky sound header track count differs: ")
                    + entry.first);
            parsed.headers.insert(entry.first, header);
            parsed.headerOrder.push_back(entry.first);
        }
    }
    if (parsed.headers.size() != 14)
        fail(QStringLiteral("Flicky sound requires 14 resident headers"));
    QSet<QString> sequenceIds;
    QSet<QString> voiceIds;
    for (const auto &header : parsed.headers) {
        voiceIds.insert(header.value.voices);
        for (const auto &track : header.value.tracks)
            sequenceIds.insert(track.sequence);
    }
    parsed.sequenceOrder = sequenceIds.values();
    parsed.voiceOrder = voiceIds.values();
    std::sort(parsed.sequenceOrder.begin(), parsed.sequenceOrder.end());
    std::sort(parsed.voiceOrder.begin(), parsed.voiceOrder.end());
    if (parsed.sequenceOrder.size() != 52 || parsed.voiceOrder.size() != 14)
        fail(QStringLiteral("Flicky sound requires 52 streams and 14 voice banks"));
    auto parseBytes = [&](const QString &id) {
        if (!parsed.blocks.contains(id))
            fail(QStringLiteral("missing Flicky sound byte label: ") + id);
        const auto range = parsed.blocks.value(id);
        ByteBlock block;
        for (const auto &line : blockLines(source, range.start,
                                           range.start + range.length)) {
            const auto directive = byteRx.match(line.content);
            if (!directive.hasMatch())
                fail(QStringLiteral("non-numeric Flicky sound byte block: ") + id);
            const auto tokens = commaTokens(directive.captured(1),
                line.absolute + directive.capturedStart(1));
            for (const auto &token : tokens) {
                block.bytes.push_back(number(token.text));
                block.tokens.push_back({token.start, token.length});
            }
        }
        if (block.bytes.isEmpty())
            fail(QStringLiteral("empty Flicky sound byte block: ") + id);
        parsed.byteBlocks.insert(id, block);
    };
    for (const auto &id : parsed.sequenceOrder) parseBytes(id);
    int voiceCount = 0;
    for (const auto &id : parsed.voiceOrder) {
        parseBytes(id);
        voiceCount += parsed.byteBlocks.value(id).bytes.size() / 25;
    }
    parseBytes(QStringLiteral("zPriorityAndEnvelopeData"));
    if (voiceCount != 34 || parsed.byteBlocks.value(
            QStringLiteral("zPriorityAndEnvelopeData")).bytes.size() != 63) {
        fail(QStringLiteral("Flicky sound FM voice or envelope inventory differs"));
    }
    return parsed;
}

QString FlickySoundDocument::source() const { return source_; }

void FlickySoundDocument::save(const QString &path) const
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        fail(QStringLiteral("cannot save Flicky sound source: ") + path);
    const auto bytes = source_.toUtf8();
    if (file.write(bytes) != bytes.size() || !file.commit())
        fail(QStringLiteral("cannot commit Flicky sound source: ") + path);
}

QStringList FlickySoundDocument::headerIds() const
{
    return parsed_.headerOrder;
}

FlickySoundHeader FlickySoundDocument::header(const QString &id) const
{
    if (!parsed_.headers.contains(id))
        fail(QStringLiteral("unknown Flicky sound header: ") + id);
    return parsed_.headers.value(id).value;
}

QStringList FlickySoundDocument::sequenceIds() const
{
    return parsed_.sequenceOrder;
}

const FlickySoundDocument::ByteBlock &FlickySoundDocument::byteBlock(
    const QString &id) const
{
    auto it = parsed_.byteBlocks.constFind(id);
    if (it == parsed_.byteBlocks.constEnd())
        fail(QStringLiteral("unknown Flicky sound byte block: ") + id);
    return it.value();
}

QVector<int> FlickySoundDocument::sequenceBytes(const QString &id) const
{
    if (!parsed_.sequenceOrder.contains(id))
        fail(QStringLiteral("unknown Flicky sound sequence: ") + id);
    return byteBlock(id).bytes;
}

QVector<FlickySoundNote> FlickySoundDocument::notes(const QString &id) const
{
    const auto data = sequenceBytes(id);
    QVector<FlickySoundNote> result;
    int cursor = 0;
    int tick = 0;
    int savedDuration = 0;
    int durationSource = -1;
    while (cursor < data.size()) {
        const int value = data[cursor];
        if (value < 0x80) {
            if (value == 0)
                fail(QStringLiteral("zero Flicky sound duration"));
            savedDuration = value;
            durationSource = cursor;
            tick += value;
            ++cursor;
            continue;
        }
        if (value < 0xE0) {
            const int noteOffset = cursor++;
            const bool explicitDuration = cursor < data.size()
                && data[cursor] < 0x80;
            if (explicitDuration) {
                if (data[cursor] == 0)
                    fail(QStringLiteral("zero Flicky sound note duration"));
                savedDuration = data[cursor];
                durationSource = cursor++;
            }
            const int duration = savedDuration > 0 ? savedDuration : 1;
            result.push_back({noteOffset, durationSource, tick, duration,
                value == 0x80 ? -1 : value - 0x81, !explicitDuration});
            tick += duration;
            continue;
        }
        const int next = cursor + 1 < data.size() ? data[cursor + 1] : 0;
        cursor = std::min(data.size(), cursor + 1 + commandOperands(value, next));
    }
    return result;
}

QStringList FlickySoundDocument::voiceBankIds() const
{
    return parsed_.voiceOrder;
}

int FlickySoundDocument::voiceCount(const QString &bank) const
{
    if (!parsed_.voiceOrder.contains(bank))
        fail(QStringLiteral("unknown Flicky FM voice bank: ") + bank);
    return byteBlock(bank).bytes.size() / 25;
}

FlickyFmVoice FlickySoundDocument::voice(const QString &bank, int index) const
{
    if (index < 0 || index >= voiceCount(bank))
        fail(QStringLiteral("Flicky FM voice index is out of range"));
    const auto bytes = byteBlock(bank).bytes;
    const int start = index * 25;
    FlickyFmVoice result;
    result.algorithm = bytes[start] & 7;
    result.feedback = (bytes[start] >> 3) & 7;
    for (int operatorIndex = 0; operatorIndex < 4; ++operatorIndex) {
        auto &op = result.operators[operatorIndex];
        const int detuneMultiple = bytes[start + 1 + operatorIndex];
        const int rateScaleAttack = bytes[start + 5 + operatorIndex];
        const int amDecay = bytes[start + 9 + operatorIndex];
        const int sustainRelease = bytes[start + 17 + operatorIndex];
        op.detune = (detuneMultiple >> 4) & 7;
        op.multiple = detuneMultiple & 15;
        op.rateScale = (rateScaleAttack >> 6) & 3;
        op.attack = rateScaleAttack & 31;
        op.amplitudeModulation = (amDecay >> 7) & 1;
        op.decay1 = amDecay & 31;
        op.decay2 = bytes[start + 13 + operatorIndex] & 31;
        op.sustain = sustainRelease >> 4;
        op.release = sustainRelease & 15;
        op.totalLevel = bytes[start + 21 + operatorIndex] & 127;
    }
    return result;
}

QVector<int> FlickySoundDocument::priorityEnvelopeBytes() const
{
    return byteBlock(QStringLiteral("zPriorityAndEnvelopeData")).bytes;
}

void FlickySoundDocument::checkShape(const Parsed &candidate) const
{
    if (candidate.headers.keys() != baselineParsed_.headers.keys()
        || candidate.sequenceOrder != baselineParsed_.sequenceOrder
        || candidate.voiceOrder != baselineParsed_.voiceOrder) {
        fail(QStringLiteral("Flicky sound inventory changed"));
    }
    for (const auto &id : baselineParsed_.headerOrder) {
        const auto before = baselineParsed_.headers.value(id).value;
        const auto after = candidate.headers.value(id).value;
        if (before.kind != after.kind || before.tracks.size() != after.tracks.size())
            fail(QStringLiteral("Flicky sound header shape changed: ") + id);
        for (int index = 0; index < before.tracks.size(); ++index) {
            if (before.tracks[index].type != after.tracks[index].type)
                fail(QStringLiteral("Flicky sound track type changed: ") + id);
        }
    }
    for (auto it = baselineParsed_.byteBlocks.constBegin();
         it != baselineParsed_.byteBlocks.constEnd(); ++it) {
        if (!candidate.byteBlocks.contains(it.key())
            || candidate.byteBlocks.value(it.key()).bytes.size()
                != it.value().bytes.size()) {
            fail(QStringLiteral("Flicky sound byte capacity changed: ") + it.key());
        }
    }
}

void FlickySoundDocument::replaceText(Span span, const QString &replacement)
{
    auto candidateSource = source_;
    candidateSource.replace(span.start, span.length, replacement);
    auto candidate = parse(candidateSource);
    checkShape(candidate);
    source_ = std::move(candidateSource);
    parsed_ = std::move(candidate);
}

void FlickySoundDocument::replaceSpan(Span span, const QString &text)
{
    replaceText(span, text);
}

void FlickySoundDocument::replaceByteTokens(
    const QString &id, const QVector<int> &replacement)
{
    const auto current = byteBlock(id);
    if (replacement.size() != current.bytes.size())
        fail(QStringLiteral("Flicky sound byte capacity cannot change"));
    auto candidateSource = source_;
    for (int index = replacement.size() - 1; index >= 0; --index) {
        if (replacement[index] == current.bytes[index]) continue;
        const auto span = current.tokens[index];
        candidateSource.replace(span.start, span.length,
                                encodedByte(replacement[index]));
    }
    auto candidate = parse(candidateSource);
    checkShape(candidate);
    source_ = std::move(candidateSource);
    parsed_ = std::move(candidate);
}

void FlickySoundDocument::setHeaderValue(
    const QString &id, const QString &field, int value)
{
    if (!parsed_.headers.contains(id))
        fail(QStringLiteral("unknown Flicky sound header: ") + id);
    const auto spans = parsed_.headers.value(id).spans;
    if (field != QStringLiteral("scale") && field != QStringLiteral("tempo"))
        fail(QStringLiteral("unsupported Flicky sound header field: ") + field);
    if (!spans.contains(field))
        fail(QStringLiteral("Flicky sound header has no field: ") + field);
    replaceSpan(spans.value(field), encodedByte(value));
}

void FlickySoundDocument::setHeaderVoices(
    const QString &id, const QString &bank)
{
    if (!parsed_.headers.contains(id) || !parsed_.voiceOrder.contains(bank))
        fail(QStringLiteral("invalid Flicky sound header or voice bank"));
    replaceSpan(parsed_.headers.value(id).spans.value(QStringLiteral("voices")),
                bank);
}

void FlickySoundDocument::setTrackValue(
    const QString &id, int track, const QString &field, int value)
{
    if (!parsed_.headers.contains(id)
        || track < 0 || track >= parsed_.headers.value(id).tracks.size()) {
        fail(QStringLiteral("invalid Flicky sound header track"));
    }
    const auto spans = parsed_.headers.value(id).tracks[track].spans;
    if (field == QStringLiteral("sequence") || !spans.contains(field))
        fail(QStringLiteral("unsupported Flicky sound track field: ") + field);
    replaceSpan(spans.value(field), encodedByte(value));
}

void FlickySoundDocument::setTrackSequence(
    const QString &id, int track, const QString &sequence)
{
    if (!parsed_.headers.contains(id)
        || track < 0 || track >= parsed_.headers.value(id).tracks.size()
        || !parsed_.sequenceOrder.contains(sequence)) {
        fail(QStringLiteral("invalid Flicky sound track sequence"));
    }
    replaceSpan(parsed_.headers.value(id).tracks[track].spans.value(
                    QStringLiteral("sequence")), sequence);
}

void FlickySoundDocument::setSequenceByte(
    const QString &id, int offset, int value)
{
    if (!parsed_.sequenceOrder.contains(id))
        fail(QStringLiteral("unknown Flicky sound sequence: ") + id);
    auto bytes = sequenceBytes(id);
    if (offset < 0 || offset >= bytes.size())
        fail(QStringLiteral("Flicky sound sequence offset is out of range"));
    bytes[offset] = value;
    replaceByteTokens(id, bytes);
}

void FlickySoundDocument::setVoice(
    const QString &bank, int index, const FlickyFmVoice &value)
{
    if (index < 0 || index >= voiceCount(bank))
        fail(QStringLiteral("Flicky FM voice index is out of range"));
    if (value.algorithm < 0 || value.algorithm > 7
        || value.feedback < 0 || value.feedback > 7) {
        fail(QStringLiteral("Flicky FM algorithm or feedback is out of range"));
    }
    auto bytes = byteBlock(bank).bytes;
    const int start = index * 25;
    bytes[start] = (bytes[start] & 0xC0)
        | (value.feedback << 3) | value.algorithm;
    for (int operatorIndex = 0; operatorIndex < 4; ++operatorIndex) {
        const auto &op = value.operators[operatorIndex];
        const int fields[] {op.detune, op.multiple, op.rateScale, op.attack,
            op.amplitudeModulation, op.decay1, op.decay2, op.sustain,
            op.release, op.totalLevel};
        const int limits[] {7, 15, 3, 31, 1, 31, 31, 15, 15, 127};
        for (int field = 0; field < 10; ++field) {
            if (fields[field] < 0 || fields[field] > limits[field])
                fail(QStringLiteral("Flicky FM operator parameter is out of range"));
        }
        bytes[start + 1 + operatorIndex] =
            (bytes[start + 1 + operatorIndex] & 0x80)
            | (op.detune << 4) | op.multiple;
        bytes[start + 5 + operatorIndex] =
            (bytes[start + 5 + operatorIndex] & 0x20)
            | (op.rateScale << 6) | op.attack;
        bytes[start + 9 + operatorIndex] =
            (bytes[start + 9 + operatorIndex] & 0x60)
            | (op.amplitudeModulation << 7) | op.decay1;
        bytes[start + 13 + operatorIndex] =
            (bytes[start + 13 + operatorIndex] & 0xE0) | op.decay2;
        bytes[start + 17 + operatorIndex] =
            (op.sustain << 4) | op.release;
        bytes[start + 21 + operatorIndex] =
            (bytes[start + 21 + operatorIndex] & 0x80) | op.totalLevel;
    }
    replaceByteTokens(bank, bytes);
}

void FlickySoundDocument::setPriorityEnvelopeByte(int offset, int value)
{
    const auto id = QStringLiteral("zPriorityAndEnvelopeData");
    auto bytes = byteBlock(id).bytes;
    if (offset < 0 || offset >= bytes.size())
        fail(QStringLiteral("Flicky priority/envelope offset is out of range"));
    bytes[offset] = value;
    replaceByteTokens(id, bytes);
}

QString FlickySoundDocument::blockText(const QString &id) const
{
    if (!parsed_.blocks.contains(id))
        fail(QStringLiteral("unknown Flicky sound block: ") + id);
    const auto span = parsed_.blocks.value(id);
    return source_.mid(span.start, span.length);
}

QString FlickySoundDocument::baselineBlockText(const QString &id) const
{
    if (!baselineParsed_.blocks.contains(id))
        fail(QStringLiteral("unknown baseline Flicky sound block: ") + id);
    const auto span = baselineParsed_.blocks.value(id);
    return baseline_.mid(span.start, span.length);
}

void FlickySoundDocument::replaceBlock(
    const QString &id, const QString &replacement)
{
    if (!parsed_.blocks.contains(id))
        fail(QStringLiteral("unknown Flicky sound block: ") + id);
    replaceText(parsed_.blocks.value(id), replacement);
}

void FlickySoundDocument::restoreBlock(const QString &id)
{
    replaceBlock(id, baselineBlockText(id));
}

} // namespace rl::flicky
