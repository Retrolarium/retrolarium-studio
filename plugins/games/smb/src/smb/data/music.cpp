#include "smb/data/music.hpp"

#include <QJsonArray>
#include <QSet>

#include <cmath>

namespace rl::smb {

namespace {

[[noreturn]] void fail(const QString& message) {
    throw SmbError(message.toStdString());
}

QVector<int> intsOf(const QJsonValue& value) {
    QVector<int> result;
    for (const QJsonValue& v : value.toArray()) result << v.toInt();
    return result;
}

constexpr double kCpu = 1789773.0;

}  // namespace

QString SmbMusic::channelLabel(const QString& channel) {
    if (channel == QLatin1String("square2")) return QStringLiteral("Square 2");
    if (channel == QLatin1String("square1")) return QStringLiteral("Square 1");
    if (channel == QLatin1String("triangle")) return QStringLiteral("Triangle");
    if (channel == QLatin1String("noise")) return QStringLiteral("Noise");
    return channel;
}

SmbMusic SmbMusic::fromJson(const QJsonObject& resource) {
    SmbMusic music;
    for (const QJsonValue& value : resource.value(QStringLiteral("headers")).toArray()) {
        const QJsonObject h = value.toObject();
        SmbMusicHeader header;
        header.name = h.value(QStringLiteral("name")).toString();
        header.lengthTableOffset = h.value(QStringLiteral("length_table_offset")).toInt();
        header.stream = h.value(QStringLiteral("stream_offset")).toInt();
        header.triangle = h.value(QStringLiteral("triangle_offset")).toInt();
        header.square1 = h.contains(QStringLiteral("square1_offset")) ? h.value(QStringLiteral("square1_offset")).toInt() : -1;
        header.noise = h.contains(QStringLiteral("noise_offset")) ? h.value(QStringLiteral("noise_offset")).toInt() : -1;
        music.headers << header;
    }
    music.arena = intsOf(resource.value(QStringLiteral("shared_stream_arena")));
    music.periods = intsOf(resource.value(QStringLiteral("note_periods")));
    music.lengths = intsOf(resource.value(QStringLiteral("note_lengths")));
    for (const QJsonValue& value : resource.value(QStringLiteral("selectors")).toArray()) {
        const QJsonObject s = value.toObject();
        music.selectors << qMakePair(s.value(QStringLiteral("name")).toString(), s.value(QStringLiteral("header")).toString());
    }
    for (const QJsonValue& value : resource.value(QStringLiteral("ground_schedule")).toArray()) {
        music.groundSchedule << value.toString();
    }
    return music;
}

QJsonObject SmbMusic::toJson(QJsonObject resource) const {
    QJsonArray array;
    for (int value : arena) array.append(value);
    resource.insert(QStringLiteral("shared_stream_arena"), array);
    return resource;
}

int SmbMusic::indexOf(const QString& header) const {
    for (int i = 0; i < headers.size(); ++i) {
        if (headers.at(i).name == header) return i;
    }
    return -1;
}

SmbMusicTimeline SmbMusic::timeline(const QString& name) const {
    const int index = indexOf(name);
    if (index < 0) fail(QStringLiteral("There is no music header %1.").arg(name));
    const SmbMusicHeader& header = headers.at(index);
    SmbMusicTimeline result;
    result.header = name;
    result.lengthBase = header.lengthTableOffset;
    const int limit = std::max(1024, int(arena.size()) * 512);
    const auto duration = [&](int lengthIndex) {
        const int position = result.lengthBase + lengthIndex;
        if (position < 0 || position >= lengths.size()) {
            fail(QStringLiteral("%1 reads past the note-length table.").arg(name));
        }
        return std::max(1, lengths.at(position));
    };
    const auto byteAt = [&](int offset, const QString& channel) {
        if (offset < 0 || offset >= arena.size()) {
            fail(QStringLiteral("%1 %2 runs off the shared arena.").arg(name, channelLabel(channel)));
        }
        return arena.at(offset);
    };

    // Square 2 sets the song's length: it plays until its zero byte.
    SmbMusicChannel square2;
    square2.id = QStringLiteral("square2");
    square2.entry = header.stream;
    int tick = 0;
    int current = -1;
    for (int cursor = header.stream;; ++cursor) {
        if (square2.events.size() > limit) fail(QStringLiteral("%1 square 2 never ends.").arg(name));
        const int raw = byteAt(cursor, square2.id);
        SmbMusicEvent e;
        e.offset = cursor;
        e.tick = tick;
        if (raw == 0) {
            e.kind = SmbMusicEvent::Kind::End;
            square2.events << e;
            break;
        }
        if (raw & 0x80) {
            e.kind = SmbMusicEvent::Kind::Length;
            e.lengthIndex = raw & 7;
            e.duration = current = duration(e.lengthIndex);
            square2.events << e;
            continue;
        }
        if (current < 0) fail(QStringLiteral("%1 square 2 has a note before its length.").arg(name));
        e.kind = SmbMusicEvent::Kind::Note;
        e.periodIndex = raw / 2;
        e.duration = current;
        square2.events << e;
        tick += current;
    }
    result.duration = tick;
    result.channels << square2;

    const auto timed = [&](const QString& id, int start) {
        SmbMusicChannel channel;
        channel.id = id;
        channel.entry = start;
        int t = 0;
        int length = -1;
        for (int cursor = start; t < result.duration; ++cursor) {
            if (channel.events.size() > limit) fail(QStringLiteral("%1 %2 never fills the song.").arg(name, channelLabel(id)));
            const int raw = byteAt(cursor, id);
            SmbMusicEvent e;
            e.offset = cursor;
            e.tick = t;
            if (id == QLatin1String("square1")) {
                if (raw == 0) {
                    e.kind = SmbMusicEvent::Kind::Control;
                    channel.events << e;
                    continue;
                }
                e.kind = SmbMusicEvent::Kind::Note;
                e.periodIndex = (raw & 0x3e) / 2;
                e.lengthIndex = ((raw & 1) << 2) | ((raw >> 6) & 3);
                e.duration = duration(e.lengthIndex);
            } else {
                if (raw & 0x80) {
                    e.kind = SmbMusicEvent::Kind::Length;
                    e.lengthIndex = raw & 7;
                    e.duration = length = duration(e.lengthIndex);
                    channel.events << e;
                    continue;
                }
                if (length < 0) fail(QStringLiteral("%1 %2 has a note before its length.").arg(name, channelLabel(id)));
                e.kind = raw == 0 ? SmbMusicEvent::Kind::Rest : SmbMusicEvent::Kind::Note;
                e.periodIndex = raw == 0 ? -1 : raw / 2;
                e.duration = length;
            }
            channel.events << e;
            t += e.duration;
        }
        return channel;
    };
    if (header.square1 > 0) {
        result.channels << timed(QStringLiteral("square1"), header.stream + header.square1);
    }
    result.channels << timed(QStringLiteral("triangle"), header.stream + header.triangle);

    if (header.noise >= 0) {
        SmbMusicChannel noise;
        noise.id = QStringLiteral("noise");
        noise.entry = header.stream + header.noise;
        int t = 0;
        int sinceLoop = 0;
        int cursor = noise.entry;
        while (t < result.duration) {
            if (noise.events.size() > limit) fail(QStringLiteral("%1 noise never fills the song.").arg(name));
            const int raw = byteAt(cursor, noise.id);
            SmbMusicEvent e;
            e.offset = cursor;
            e.tick = t;
            if (raw == 0) {
                if (sinceLoop == 0) fail(QStringLiteral("%1 noise loops without a beat.").arg(name));
                e.kind = SmbMusicEvent::Kind::Loop;
                noise.events << e;
                cursor = noise.entry;
                sinceLoop = 0;
                continue;
            }
            e.kind = SmbMusicEvent::Kind::Beat;
            e.beat = (raw >> 4) & 3;
            e.lengthIndex = ((raw & 1) << 2) | ((raw >> 6) & 3);
            e.duration = duration(e.lengthIndex);
            noise.events << e;
            t += e.duration;
            ++cursor;
            ++sinceLoop;
        }
        result.channels << noise;
    }
    return result;
}

QStringList SmbMusic::readersOf(int offset) const {
    if (!readersReady_) {
        readers_.clear();
        for (const SmbMusicHeader& header : headers) {
            try {
                const SmbMusicTimeline t = timeline(header.name);
                for (const SmbMusicChannel& channel : t.channels) {
                    QSet<int> seen;
                    for (const SmbMusicEvent& e : channel.events) {
                        if (seen.contains(e.offset)) continue;
                        seen.insert(e.offset);
                        const QString reader = QStringLiteral("%1 %2").arg(header.name, channelLabel(channel.id).toLower());
                        if (!readers_[e.offset].contains(reader)) readers_[e.offset] << reader;
                    }
                }
            } catch (const SmbError&) {
            }
        }
        readersReady_ = true;
    }
    return readers_.value(offset);
}

int SmbMusic::pitchOf(int periodIndex, const QString& channel) const {
    if (periodIndex < 0 || periodIndex >= periods.size()) {
        return -1;
    }
    // As the kit reads the table: tiny periods and a zero low byte are
    // silence, the timer is 11 bits.
    const int period = periods.at(periodIndex);
    if (period < 8 || (period & 0xff) == 0) {
        return -1;
    }
    const double divider = channel == QLatin1String("triangle") ? 32.0 : 16.0;
    const double frequency = kCpu / (divider * ((period & 0x7ff) + 1));
    return int(std::lround(69.0 + 12.0 * std::log2(frequency / 440.0)));
}

int SmbMusic::periodFor(int pitch, const QString& channel) const {
    // Square 1 reaches only the first 32 entries; 0 is not a note on
    // square 2 and the triangle (the byte would end or rest the stream).
    const int last = channel == QLatin1String("square1") ? std::min(31, int(periods.size()) - 1) : int(periods.size()) - 1;
    for (int i = channel == QLatin1String("square1") ? 0 : 1; i <= last; ++i) {
        if (pitchOf(i, channel) == pitch) return i;
    }
    return -1;
}

void SmbMusic::setNotePeriod(const QString& channel, int offset, int periodIndex) {
    if (offset < 0 || offset >= arena.size()) fail(QStringLiteral("No such stream byte."));
    const int raw = arena.at(offset);
    int next = raw;
    if (channel == QLatin1String("square1")) {
        if (raw == 0 || periodIndex < 0 || periodIndex > 31) fail(QStringLiteral("Square 1 notes use period entries 0-31."));
        next = (raw & 0xc1) | (periodIndex * 2);
        if (next == 0) fail(QStringLiteral("That note would read as a control byte."));
    } else if (channel == QLatin1String("square2") || channel == QLatin1String("triangle")) {
        if (raw == 0 || (raw & 0x80)) fail(QStringLiteral("Only a note byte takes a pitch."));
        if (periodIndex < 1 || periodIndex > 63) fail(QStringLiteral("Notes use period entries 1-63."));
        next = periodIndex * 2;
    } else {
        fail(QStringLiteral("The noise channel has beats, not pitches."));
    }
    arena[offset] = next;
    readersReady_ = false;
}

void SmbMusic::setEventLength(const QString& channel, int offset, int lengthIndex) {
    if (offset < 0 || offset >= arena.size()) fail(QStringLiteral("No such stream byte."));
    if (lengthIndex < 0 || lengthIndex > 7) fail(QStringLiteral("A length picks entry 0-7 of the song's length row."));
    const int raw = arena.at(offset);
    int next = raw;
    if (channel == QLatin1String("square1") || channel == QLatin1String("noise")) {
        if (raw == 0) fail(QStringLiteral("That byte is not a note."));
        next = (raw & 0x3e) | ((lengthIndex & 3) << 6) | (lengthIndex >> 2);
        if (next == 0) fail(QStringLiteral("That length would turn the byte into a control byte."));
    } else {
        if (!(raw & 0x80)) fail(QStringLiteral("Only a length byte sets the length on this channel."));
        next = 0x80 | lengthIndex;
    }
    arena[offset] = next;
    readersReady_ = false;
}

void SmbMusic::setBeat(int offset, int beat) {
    if (offset < 0 || offset >= arena.size() || arena.at(offset) == 0) fail(QStringLiteral("That byte is not a beat."));
    const int next = (arena.at(offset) & 0xc1) | ((beat & 3) << 4);
    if (next == 0) fail(QStringLiteral("That beat would read as the loop marker."));
    arena[offset] = next;
    readersReady_ = false;
}

}  // namespace rl::smb
