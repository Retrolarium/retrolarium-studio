#include "castle/data/music.hpp"

#include "castle/data/levels.hpp"
#include "sdk/i18n.hpp"

#include <QJsonArray>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace rl::castle {

namespace {

constexpr double kCpuFrequency = 1789773.0;
// Offsets into the playback prefix ($BB16): note semitones, note lengths
// and the APU period table.
constexpr int kSemitones = 0;
constexpr int kLengths = 7;
constexpr int kPeriods = 0x3f;

int noteIndex(char letter) {
    return letter >= 'a' && letter <= 'g' ? letter - 'a' : -1;
}

int defaultLengthIndex(char c) {
    switch (c) {
    case '@': return 0;
    case '8': return 1;
    case '4': return 2;
    case '2': return 3;
    case '1': return 4;
    default: return -1;
    }
}

// The playback prefix and the packed arena, as the engine reads them.
QByteArray memoryOf(const QJsonObject& music) {
    QByteArray memory;
    for (const QJsonValue& v : music.value(QStringLiteral("playback_prefix")).toArray()) {
        memory.append(static_cast<char>(v.toInt()));
    }
    const QJsonArray streams = music.value(QStringLiteral("streams")).toArray();
    QHash<QString, int> starts;
    const int mml = music.value(QStringLiteral("mml_cpu_address")).toInt();
    int cursor = 0;
    for (const QJsonValue& v : streams) {
        starts.insert(v.toObject().value(QStringLiteral("id")).toString(), mml + cursor);
        cursor += v.toObject().value(QStringLiteral("text")).toString().size() + 3;
    }
    for (const QJsonValue& v : streams) {
        const QJsonObject s = v.toObject();
        memory.append(s.value(QStringLiteral("text")).toString().toLatin1());
        memory.append('\0');
        const int restart = starts.value(s.value(QStringLiteral("restart")).toString(), 0);
        memory.append(static_cast<char>(restart & 0xff));
        memory.append(static_cast<char>(restart >> 8));
    }
    return memory;
}

int byteAt(const QByteArray& memory, int offset) {
    return offset >= 0 && offset < memory.size() ? static_cast<quint8>(memory.at(offset)) : 0;
}

int pitchFromPeriod(const QByteArray& memory, int index, int channel) {
    const int period = byteAt(memory, kPeriods + index) | (byteAt(memory, kPeriods + index + 1) << 8);
    const double frequency = kCpuFrequency / ((channel == 2 ? 32.0 : 16.0) * (period + 1));
    return static_cast<int>(std::lround(69.0 + 12.0 * std::log2(frequency / 440.0)));
}

int periodIndex(const QByteArray& memory, char letter, char accidental, int octave, int channel) {
    const int semitone = byteAt(memory, kSemitones + noteIndex(letter));
    int index = ((((semitone + octave) & 0xff) + 1) << 1) & 0xff;
    if (accidental == '+') index = (index + 2) & 0xff;
    if (accidental == '-') index = (index - 2) & 0xff;
    if (channel == 2) index = (index + 0x18) & 0xff;
    return index;
}

struct Cursor {
    int stream = -1;        // -1: stopped
    int position = 0;
    int ticks = 1;
    int octave = 0;
    int lengthIndex = 0;
};

}  // namespace

int CastleMusic::pitchOf(const QJsonObject& music, char letter, char accidental, int octave, int channel) {
    if (noteIndex(letter) < 0) return -1;
    const QByteArray memory = memoryOf(music);
    return pitchFromPeriod(memory, periodIndex(memory, letter, accidental, octave, channel), channel);
}

QVector<QVector<CastleMusicNote>> CastleMusic::trace(const QJsonObject& music, int track, int frames) {
    QVector<QVector<CastleMusicNote>> lanes(kChannels);
    const QJsonArray streams = music.value(QStringLiteral("streams")).toArray();
    const QJsonObject set = music.value(QStringLiteral("track_sets")).toArray().at(track).toObject();
    QHash<QString, int> ids;
    QVector<QByteArray> texts;
    for (int i = 0; i < streams.size(); ++i) {
        const QJsonObject s = streams.at(i).toObject();
        ids.insert(s.value(QStringLiteral("id")).toString(), i);
        texts << s.value(QStringLiteral("text")).toString().toLatin1();
    }
    QVector<int> restarts;
    for (const QJsonValue& v : streams) {
        restarts << ids.value(v.toObject().value(QStringLiteral("restart")).toString(), -1);
    }
    const QByteArray memory = memoryOf(music);
    const QJsonArray pointers = set.value(QStringLiteral("streams")).toArray();
    Cursor cursors[kChannels];
    for (int c = 0; c < kChannels; ++c) {
        cursors[c].stream = pointers.at(c).isString() ? ids.value(pointers.at(c).toString(), -1) : -1;
    }
    const auto at = [&texts](const Cursor& cur, int position) -> char {
        const QByteArray& text = texts.at(cur.stream);
        return position < text.size() ? text.at(position) : '\0';
    };
    int tempo = 0;
    for (int frame = 0; frame < frames; ++frame) {
        bool any = false;
        for (int c = 0; c < kChannels; ++c) {
            Cursor& cur = cursors[c];
            if (cur.stream < 0) continue;
            any = true;
            cur.ticks = (cur.ticks - 1) & 0xff;
            if (cur.ticks != 0) continue;
            QSet<int> visited;
            for (;;) {
                if (cur.position >= texts.at(cur.stream).size()) {
                    // The terminator and the restart pointer.
                    const int restart = restarts.value(cur.stream, -1);
                    if (restart < 0 || visited.contains(restart)) {
                        cur.stream = -1;
                        break;
                    }
                    visited.insert(restart);
                    cur.stream = restart;
                    cur.position = 0;
                    continue;
                }
                const char command = at(cur, cur.position);
                int p = cur.position + 1;
                if (command == 'o') {
                    cur.octave = ((static_cast<quint8>(at(cur, p)) - 0x32) * 12) & 0xff;
                    cur.position = p + 1;
                    continue;
                }
                if (command == 't') {
                    tempo = ((static_cast<quint8>(at(cur, p)) - '0') * 8) & 0xff;
                    cur.position = p + 1;
                    continue;
                }
                if (command == 'l') {
                    const int index = defaultLengthIndex(at(cur, p));
                    cur.lengthIndex = index < 0 ? 4 : index;
                    cur.position = p + 1;
                    continue;
                }
                if (command == 'v') {
                    cur.position = p + 1;
                    continue;
                }
                CastleMusicNote note;
                note.start = frame;
                note.stream = cur.stream;
                note.position = cur.position;
                note.octave = cur.octave;
                if (noteIndex(command) >= 0) {
                    char accidental = 0;
                    if (at(cur, p) == '+' || at(cur, p) == '-') accidental = at(cur, p++);
                    note.pitch = pitchFromPeriod(memory, periodIndex(memory, command, accidental, cur.octave, c), c);
                } else if (command != 'r') {
                    cur.stream = -1;     // not MML the engine plays
                    break;
                }
                const char duration = at(cur, p);
                int ticks = 0;
                if (duration == '@' || (duration >= '0' && duration <= '9')) {
                    const int fixed = defaultLengthIndex(duration);
                    ++p;
                    ticks = fixed < 0 ? byteAt(memory, kLengths + ((tempo + cur.lengthIndex) & 0xff))
                                      : byteAt(memory, kLengths + ((tempo + fixed) & 0xff));
                } else {
                    ticks = byteAt(memory, kLengths + ((tempo + cur.lengthIndex) & 0xff));
                }
                if (at(cur, p) == '.') {
                    ticks = (ticks + (ticks >> 1)) & 0xff;
                    ++p;
                }
                cur.position = p;
                cur.ticks = ticks;
                note.span = p - note.position;
                note.length = std::min(std::max(1, ticks), frames - frame);
                lanes[c] << note;
                break;
            }
        }
        if (!any) break;
    }
    return lanes;
}

QString CastleMusic::checkText(const QString& text) {
    if (text.isEmpty()) return rlTr("castle", "A stream needs at least one command.");
    const QByteArray data = text.toLatin1();
    if (QString::fromLatin1(data) != text) return rlTr("castle", "MML is plain ASCII.");
    for (int i = 0; i < data.size();) {
        const char c = data.at(i++);
        if (c == 'o' || c == 't' || c == 'l' || c == 'v') {
            if (i >= data.size()) return rlTr("castle", "'%1' needs an argument.").arg(QLatin1Char(c));
            const char a = data.at(i++);
            if ((c == 'o' || c == 't') && !(a >= '0' && a <= '9'))
                return rlTr("castle", "'%1' takes a digit.").arg(QLatin1Char(c));
            if (c == 'l' && defaultLengthIndex(a) < 0) return rlTr("castle", "'l' takes @, 8, 4, 2 or 1.");
            if (c == 'v' && !((a >= '0' && a <= '9') || (a >= 'a' && a <= 'f')))
                return rlTr("castle", "'v' takes 0-9 or a-f.");
            continue;
        }
        if (noteIndex(c) < 0 && c != 'r') {
            return rlTr("castle", "'%1' at %2 is not MML (notes a-g, r, o l t v commands).")
                .arg(QLatin1Char(c))
                .arg(i - 1);
        }
        if (noteIndex(c) >= 0 && i < data.size() && (data.at(i) == '+' || data.at(i) == '-')) ++i;
        if (i < data.size() && defaultLengthIndex(data.at(i)) >= 0) ++i;
        if (i < data.size() && data.at(i) == '.') ++i;
    }
    return {};
}

int CastleMusic::usedBytes(const QJsonObject& music) {
    int used = 0;
    for (const QJsonValue& v : music.value(QStringLiteral("streams")).toArray()) {
        used += v.toObject().value(QStringLiteral("text")).toString().toLatin1().size() + 3;
    }
    return used;
}

QJsonObject CastleMusic::withText(const QJsonObject& music, int stream, const QString& text) {
    const QString problem = checkText(text);
    if (!problem.isEmpty()) throw CastleError(problem.toStdString());
    QJsonObject next = music;
    QJsonArray streams = next.value(QStringLiteral("streams")).toArray();
    if (stream < 0 || stream >= streams.size()) throw CastleError("No such music stream.");
    QJsonObject s = streams.at(stream).toObject();
    s.insert(QStringLiteral("text"), text);
    streams.replace(stream, s);
    next.insert(QStringLiteral("streams"), streams);
    const int used = usedBytes(next);
    const int capacity = next.value(QStringLiteral("capacity_bytes")).toInt();
    if (used > capacity) {
        throw CastleError(rlTr("castle", "The music would take %1 of %2 bytes; shorten a stream by %3 first.")
                              .arg(used)
                              .arg(capacity)
                              .arg(used - capacity)
                              .toStdString());
    }
    next.insert(QStringLiteral("used_bytes"), used);
    return next;
}

QString CastleMusic::retune(const QJsonObject& music, const CastleMusicNote& note, int channel, int pitch,
                            const QString& text) {
    if (note.pitch < 0 || note.position >= text.size()) return {};
    const QByteArray memory = memoryOf(music);
    const auto find = [&](int octave, QString* token) {
        for (const char accidental : {'\0', '+', '-'}) {
            for (const char letter : {'c', 'd', 'e', 'f', 'g', 'a', 'b'}) {
                if (pitchFromPeriod(memory, periodIndex(memory, letter, accidental, octave, channel), channel) == pitch) {
                    *token = QString(QLatin1Char(letter)) + (accidental ? QString(QLatin1Char(accidental)) : QString());
                    return true;
                }
            }
        }
        return false;
    };
    const bool hasAccidental = note.position + 1 < text.size() &&
                               (text.at(note.position + 1) == QLatin1Char('+') || text.at(note.position + 1) == QLatin1Char('-'));
    const int letterSpan = hasAccidental ? 2 : 1;
    QString token;
    if (find(note.octave, &token)) {
        return text.left(note.position) + token + text.mid(note.position + letterSpan);
    }
    const auto octaveOf = [](QChar digit) { return ((digit.toLatin1() - 0x32) * 12) & 0xff; };
    const auto isSwitch = [&](int at) {
        return at >= 0 && at + 1 < text.size() && text.at(at) == QLatin1Char('o') && text.at(at + 1) >= QLatin1Char('0') &&
               text.at(at + 1) <= QLatin1Char('9');
    };
    const QString rest = text.mid(note.position + letterSpan, note.span - letterSpan);
    // Already switched for this note alone (oN note oM): change that switch,
    // or drop both when the note goes back to the octave around it, rather
    // than nesting another pair each time.
    const int before = note.position - 2;
    const int after = note.position + note.span;
    if (isSwitch(before) && isSwitch(after) && octaveOf(text.at(before + 1)) == note.octave) {
        const int outer = octaveOf(text.at(after + 1));
        for (char digit = '0'; digit <= '9'; ++digit) {
            const int octave = octaveOf(QLatin1Char(digit));
            if (octave == note.octave || !find(octave, &token)) continue;
            if (octave == outer) return text.left(before) + token + rest + text.mid(after + 2);
            return text.left(before) + QLatin1Char('o') + QLatin1Char(digit) + token + rest + text.mid(after);
        }
        return {};
    }
    // Another octave: switch to it for this note and back after it.
    char back = 0;
    for (char digit = '0'; digit <= '9'; ++digit) {
        if ((((digit - 0x32) * 12) & 0xff) == note.octave) back = digit;
    }
    if (!back) return {};
    for (char digit = '0'; digit <= '9'; ++digit) {
        const int octave = ((digit - 0x32) * 12) & 0xff;
        if (octave == note.octave || !find(octave, &token)) continue;
        return text.left(note.position) + QLatin1Char('o') + QLatin1Char(digit) + token + rest + QLatin1Char('o') + QLatin1Char(back) +
               text.mid(note.position + note.span);
    }
    return {};
}

}  // namespace rl::castle
