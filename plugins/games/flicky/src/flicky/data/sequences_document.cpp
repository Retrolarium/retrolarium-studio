#include <retrolarium/flicky_sequences_document.hpp>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>

#include <array>

namespace retrolarium {
namespace {

[[noreturn]] void fail(const QString &message)
{
    throw FlickySequencesDocumentError(message.toStdString());
}

int integer(const QJsonValue &value, int minimum, int maximum,
            const QString &context)
{
    if (!value.isDouble() || value.toDouble() != value.toInt(-1)
        || value.toInt(-1) < minimum || value.toInt(-1) > maximum) {
        fail(QStringLiteral("invalid Flicky sequence integer: ") + context);
    }
    return value.toInt();
}

QStringList ids(const QJsonObject &root, const char *section)
{
    QStringList result;
    for (const auto &value : root.value(QLatin1String(section)).toArray())
        result.push_back(value.toObject().value(QStringLiteral("id")).toString());
    return result;
}

void validatePiece(const QJsonObject &piece)
{
    for (const auto *key : {"x", "flipped_x", "y"})
        (void)integer(piece.value(QLatin1String(key)), -128, 127,
                      QLatin1String(key));
    for (const auto *key : {"width", "height"})
        (void)integer(piece.value(QLatin1String(key)), 1, 4,
                      QLatin1String(key));
    (void)integer(piece.value(QStringLiteral("tile")), 0, 0x7FF,
                  QStringLiteral("tile"));
    (void)integer(piece.value(QStringLiteral("palette")), 0, 3,
                  QStringLiteral("palette"));
    for (const auto *key : {"priority", "hflip", "vflip"}) {
        if (!piece.value(QLatin1String(key)).isBool())
            fail(QStringLiteral("invalid Flicky sprite flag: ") + QLatin1String(key));
    }
}

bool sourceIsKnown(const QString &source)
{
    static const QSet<QString> sources {
        QStringLiteral("game/bonus/objects.s"),
        QStringLiteral("game/actors/chirp.s"),
        QStringLiteral("game/actors/window_girl_and_throwable.s"),
        QStringLiteral("game/enemies/tiger.s"),
        QStringLiteral("game/actors/player.s"),
        QStringLiteral("game/enemies/lizard.s"),
        QStringLiteral("game/enemies/spawner.s"),
        QStringLiteral("rendering/hud.s"),
    };
    return sources.contains(source);
}

} // namespace

FlickySequencesDocument FlickySequencesDocument::load(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        fail(QStringLiteral("cannot open Flicky sequences document: ") + path);
    QJsonParseError error;
    const auto json = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !json.isObject())
        fail(QStringLiteral("invalid Flicky sequences JSON: ") + error.errorString());
    return fromJson(json.object());
}

FlickySequencesDocument FlickySequencesDocument::fromJson(const QJsonObject &root)
{
    validate(root);
    FlickySequencesDocument document;
    document.root_ = root;
    document.baseline_ = root;
    return document;
}

void FlickySequencesDocument::validate(const QJsonObject &root)
{
    if (root.value(QStringLiteral("schema_version")).toInt(-1) != 2
        || root.value(QStringLiteral("profile")).toString()
               != QStringLiteral("canonical")) {
        fail(QStringLiteral("unsupported Flicky sequences schema or profile"));
    }
    const auto mappings = root.value(QStringLiteral("mappings")).toArray();
    const auto animations = root.value(QStringLiteral("animations")).toArray();
    if (mappings.size() != 224 || animations.size() != 50)
        fail(QStringLiteral("Flicky sequences need 224 mappings and 50 animations"));
    QSet<QString> mappingIds;
    for (const auto &value : mappings) {
        if (!value.isObject()) fail(QStringLiteral("invalid Flicky mapping record"));
        const auto mapping = value.toObject();
        const auto id = mapping.value(QStringLiteral("id")).toString();
        if (id.isEmpty() || mappingIds.contains(id)
            || mapping.value(QStringLiteral("source")).toString()
                   != QStringLiteral("data/tables.s")) {
            fail(QStringLiteral("invalid Flicky mapping identity or source"));
        }
        mappingIds.insert(id);
        (void)integer(mapping.value(QStringLiteral("render_flags")), 0, 255,
                      QStringLiteral("render_flags"));
        const auto pieces = mapping.value(QStringLiteral("pieces")).toArray();
        if (pieces.isEmpty() || pieces.size() > 32)
            fail(QStringLiteral("invalid Flicky sprite piece count"));
        for (const auto &piece : pieces) {
            if (!piece.isObject()) fail(QStringLiteral("invalid Flicky sprite piece"));
            validatePiece(piece.toObject());
        }
    }
    QSet<QString> animationIds;
    for (const auto &value : animations) {
        if (!value.isObject()) fail(QStringLiteral("invalid Flicky animation record"));
        const auto animation = value.toObject();
        const auto id = animation.value(QStringLiteral("id")).toString();
        const auto kind = animation.value(QStringLiteral("kind")).toString();
        if (id.isEmpty() || animationIds.contains(id)
            || !sourceIsKnown(animation.value(QStringLiteral("source")).toString())
            || (kind != QStringLiteral("sprite")
                && kind != QStringLiteral("tilemap"))) {
            fail(QStringLiteral("invalid Flicky animation identity, source, or kind"));
        }
        animationIds.insert(id);
        (void)integer(animation.value(QStringLiteral("delay")), 0, 255,
                      QStringLiteral("delay"));
        const auto frames = animation.value(QStringLiteral("frames")).toArray();
        if (frames.isEmpty() || frames.size() > 255)
            fail(QStringLiteral("invalid Flicky animation frame count"));
        for (const auto &frame : frames) {
            if (!frame.isString() || frame.toString().isEmpty())
                fail(QStringLiteral("invalid Flicky animation frame reference"));
        }
    }
    if (root.contains(QStringLiteral("bonus_motion"))) {
        const auto motion = root.value(QStringLiteral("bonus_motion")).toArray();
        if (motion.size() != 30)
            fail(QStringLiteral("Flicky bonus motion needs 30 fixed tables"));
        static const std::array<int, 9> trajectoryLengths {1, 5, 9, 7, 9, 9, 3, 7, 5};
        int recordIndex = 0;
        const auto validateGroup = [&](const char *prefix, const char *kind,
                                       int count, int length) {
            for (int tableIndex = 0; tableIndex < count; ++tableIndex) {
                const auto entry = motion.at(recordIndex++).toObject();
                const QString id = QString::fromLatin1(prefix) + QString::number(tableIndex);
                const int expectedLength = length > 0 ? length
                    : trajectoryLengths[static_cast<std::size_t>(tableIndex)];
                const auto words = entry.value(QStringLiteral("words")).toArray();
                if (entry.value(QStringLiteral("id")).toString() != id
                    || entry.value(QStringLiteral("source")).toString()
                        != QStringLiteral("game/bonus/objects.s")
                    || entry.value(QStringLiteral("kind")).toString()
                        != QString::fromLatin1(kind)
                    || words.size() != expectedLength)
                    fail(QStringLiteral("invalid Flicky bonus table identity/shape: ") + id);
                if (entry.contains(QStringLiteral("rounds"))) {
                    if (QString::fromLatin1(kind) == QStringLiteral("trajectory"))
                        fail(QStringLiteral("indirect Flicky trajectory has no fixed rounds: ") + id);
                    if (!entry.value(QStringLiteral("rounds")).isArray())
                        fail(QStringLiteral("invalid Flicky bonus round aliases: ") + id);
                    const auto rounds = entry.value(QStringLiteral("rounds")).toArray();
                    QSet<int> uniqueRounds;
                    for (const auto &value : rounds) {
                        const int round = integer(value, 3, 47, id);
                        if (round % 4 != 3 || uniqueRounds.contains(round))
                            fail(QStringLiteral("invalid Flicky bonus round alias: ") + id);
                        uniqueRounds.insert(round);
                    }
                }
                for (int index = 0; index < words.size(); ++index) {
                    const int word = integer(words.at(index), 0, 0xFFFF, id);
                    if (QString::fromLatin1(kind) == QStringLiteral("trajectory_index")
                        && ((word >> 8) > 8 || (word & 0xFF) > 8))
                        fail(QStringLiteral("invalid Flicky bonus trajectory index: ") + id);
                }
                if (QString::fromLatin1(kind) == QStringLiteral("trajectory")
                    && words.last().toInt() != 0xFFFF)
                    fail(QStringLiteral("invalid Flicky bonus trajectory sentinel: ") + id);
            }
        };
        validateGroup("Bonus_ChickDelays", "delay", 3, 20);
        validateGroup("Bonus_ChickVelocities", "velocity", 9, 20);
        validateGroup("Bonus_ChickTrajIndex", "trajectory_index", 9, 10);
        validateGroup("Bonus_Trajectory", "trajectory", 9, 0);
    }
}

QJsonObject FlickySequencesDocument::toJson() const { return root_; }

void FlickySequencesDocument::save(const QString &path) const
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        fail(QStringLiteral("cannot save Flicky sequences: ") + path);
    const auto bytes = QJsonDocument(root_).toJson(QJsonDocument::Indented);
    if (file.write(bytes) != bytes.size() || !file.commit())
        fail(QStringLiteral("cannot commit Flicky sequences: ") + path);
}

int FlickySequencesDocument::recordIndex(
    const QJsonObject &root, const char *section, const QString &id)
{
    const auto records = root.value(QLatin1String(section)).toArray();
    for (int index = 0; index < records.size(); ++index) {
        if (records.at(index).toObject().value(QStringLiteral("id")).toString() == id)
            return index;
    }
    fail(QStringLiteral("unknown Flicky sequence record: ") + id);
}

QJsonObject FlickySequencesDocument::record(
    const QJsonObject &root, const char *section, const QString &id)
{
    return root.value(QLatin1String(section)).toArray()
        .at(recordIndex(root, section, id)).toObject();
}

void FlickySequencesDocument::replaceRecord(
    const char *section, const QString &id, const QJsonObject &replacement)
{
    auto records = root_.value(QLatin1String(section)).toArray();
    records.replace(recordIndex(root_, section, id), replacement);
    auto candidate = root_;
    candidate.insert(QLatin1String(section), records);
    validate(candidate);
    root_ = candidate;
}

QStringList FlickySequencesDocument::mappingIds() const
{
    return ids(root_, "mappings");
}

QJsonObject FlickySequencesDocument::mapping(const QString &id) const
{
    return record(root_, "mappings", id);
}

QJsonObject FlickySequencesDocument::baselineMapping(const QString &id) const
{
    return record(baseline_, "mappings", id);
}

void FlickySequencesDocument::replaceMapping(
    const QString &id, const QJsonObject &replacement)
{
    const auto current = mapping(id);
    if (replacement.value(QStringLiteral("id")) != current.value(QStringLiteral("id"))
        || replacement.value(QStringLiteral("source"))
            != current.value(QStringLiteral("source"))
        || replacement.value(QStringLiteral("pieces")).toArray().size()
            != current.value(QStringLiteral("pieces")).toArray().size()) {
        fail(QStringLiteral("Flicky mapping identity or piece count cannot change"));
    }
    replaceRecord("mappings", id, replacement);
}

void FlickySequencesDocument::setMappingRenderFlags(const QString &id, int value)
{
    auto next = mapping(id);
    next.insert(QStringLiteral("render_flags"), value);
    replaceMapping(id, next);
}

void FlickySequencesDocument::setMappingPiece(
    const QString &id, int index, const QJsonObject &piece)
{
    auto next = mapping(id);
    auto pieces = next.value(QStringLiteral("pieces")).toArray();
    if (index < 0 || index >= pieces.size())
        fail(QStringLiteral("Flicky sprite piece index is outside its mapping"));
    auto merged = pieces.at(index).toObject();
    for (auto it = piece.constBegin(); it != piece.constEnd(); ++it) {
        if (!QStringList {QStringLiteral("x"), QStringLiteral("flipped_x"),
                QStringLiteral("y"), QStringLiteral("width"),
                QStringLiteral("height"), QStringLiteral("tile"),
                QStringLiteral("palette"), QStringLiteral("priority"),
                QStringLiteral("hflip"), QStringLiteral("vflip")}.contains(it.key())) {
            fail(QStringLiteral("unknown Flicky sprite piece field: ") + it.key());
        }
        merged.insert(it.key(), it.value());
    }
    pieces.replace(index, merged);
    next.insert(QStringLiteral("pieces"), pieces);
    replaceMapping(id, next);
}

void FlickySequencesDocument::restoreMapping(const QString &id)
{
    replaceMapping(id, baselineMapping(id));
}

QStringList FlickySequencesDocument::animationIds() const
{
    return ids(root_, "animations");
}

QJsonObject FlickySequencesDocument::animation(const QString &id) const
{
    return record(root_, "animations", id);
}

QJsonObject FlickySequencesDocument::baselineAnimation(const QString &id) const
{
    return record(baseline_, "animations", id);
}

QStringList FlickySequencesDocument::allowedFrames(const QString &kind) const
{
    if (kind != QStringLiteral("sprite") && kind != QStringLiteral("tilemap"))
        fail(QStringLiteral("unknown Flicky animation kind: ") + kind);
    QSet<QString> seen;
    QStringList frames;
    for (const auto &value : baseline_.value(QStringLiteral("animations")).toArray()) {
        const auto record = value.toObject();
        if (record.value(QStringLiteral("kind")).toString() != kind) continue;
        for (const auto &frame : record.value(QStringLiteral("frames")).toArray()) {
            const auto name = frame.toString();
            if (!seen.contains(name)) {
                frames.push_back(name);
                seen.insert(name);
            }
        }
    }
    return frames;
}

void FlickySequencesDocument::replaceAnimation(
    const QString &id, const QJsonObject &replacement)
{
    const auto current = animation(id);
    if (replacement.value(QStringLiteral("id")) != current.value(QStringLiteral("id"))
        || replacement.value(QStringLiteral("source"))
            != current.value(QStringLiteral("source"))
        || replacement.value(QStringLiteral("kind"))
            != current.value(QStringLiteral("kind"))
        || replacement.value(QStringLiteral("frames")).toArray().size()
            != current.value(QStringLiteral("frames")).toArray().size()) {
        fail(QStringLiteral("Flicky animation identity or frame count cannot change"));
    }
    const auto kind = current.value(QStringLiteral("kind")).toString();
    const auto allowed = allowedFrames(kind);
    for (const auto &frame : replacement.value(QStringLiteral("frames")).toArray()) {
        if (!allowed.contains(frame.toString()))
            fail(QStringLiteral("Flicky animation frame is not in source inventory"));
    }
    replaceRecord("animations", id, replacement);
}

void FlickySequencesDocument::setAnimationDelay(const QString &id, int value)
{
    auto next = animation(id);
    next.insert(QStringLiteral("delay"), value);
    replaceAnimation(id, next);
}

void FlickySequencesDocument::setAnimationFrame(
    const QString &id, int index, const QString &frame)
{
    auto next = animation(id);
    const auto kind = next.value(QStringLiteral("kind")).toString();
    if (!allowedFrames(kind).contains(frame))
        fail(QStringLiteral("Flicky animation frame is not in source inventory"));
    auto frames = next.value(QStringLiteral("frames")).toArray();
    if (index < 0 || index >= frames.size())
        fail(QStringLiteral("Flicky animation frame index is outside its sequence"));
    frames.replace(index, frame);
    next.insert(QStringLiteral("frames"), frames);
    replaceAnimation(id, next);
}

void FlickySequencesDocument::restoreAnimation(const QString &id)
{
    replaceAnimation(id, baselineAnimation(id));
}

QStringList FlickySequencesDocument::bonusMotionIds() const
{
    return ids(root_, "bonus_motion");
}

QJsonObject FlickySequencesDocument::bonusMotion(const QString &id) const
{
    return record(root_, "bonus_motion", id);
}

QJsonObject FlickySequencesDocument::baselineBonusMotion(const QString &id) const
{
    return record(baseline_, "bonus_motion", id);
}

void FlickySequencesDocument::replaceBonusMotion(
    const QString &id, const QJsonObject &replacement)
{
    const auto current = bonusMotion(id);
    for (const auto *key : {"id", "source", "kind", "rounds"})
        if (replacement.value(QLatin1String(key)) != current.value(QLatin1String(key)))
            fail(QStringLiteral("Flicky bonus table identity cannot change"));
    if (replacement.value(QStringLiteral("words")).toArray().size()
        != current.value(QStringLiteral("words")).toArray().size())
        fail(QStringLiteral("Flicky bonus table word count cannot change"));
    replaceRecord("bonus_motion", id, replacement);
}

void FlickySequencesDocument::setBonusMotionWord(
    const QString &id, int index, int value)
{
    auto next = bonusMotion(id);
    auto words = next.value(QStringLiteral("words")).toArray();
    if (index < 0 || index >= words.size())
        fail(QStringLiteral("Flicky bonus word index is outside its table"));
    words.replace(index, value);
    next.insert(QStringLiteral("words"), words);
    replaceBonusMotion(id, next);
}

void FlickySequencesDocument::restoreBonusMotion(const QString &id)
{
    replaceBonusMotion(id, baselineBonusMotion(id));
}

} // namespace retrolarium
