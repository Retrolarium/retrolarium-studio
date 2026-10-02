#pragma once

#include "smb/data/level_records.hpp"

#include <QHash>
#include <QJsonObject>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QVector>

namespace rl::smb {

// One music header of smb.apu-music.v1: where its channels start in the
// shared stream arena, and which row of the note-length table it uses.
struct SmbMusicHeader {
    QString name;
    int lengthTableOffset = 0;
    int stream = 0;             // square 2 entry, arena offset
    int triangle = 0;           // relative to stream
    int square1 = -1;           // relative; -1 absent, 0 silent
    int noise = -1;             // relative; -1 absent
};

// One decoded event of a channel, at the arena byte that holds it.
struct SmbMusicEvent {
    enum class Kind { Note, Rest, Beat, Length, Control, Loop, End };
    Kind kind = Kind::Note;
    int offset = 0;             // arena byte
    int tick = 0;               // engine ticks (frames) from the song start
    int duration = 0;
    int periodIndex = -1;       // notes
    int lengthIndex = -1;       // length commands, square 1 notes and beats
    int beat = 0;               // noise: 0 rest, 1 short, 2 strong, 3 long
};

struct SmbMusicChannel {
    QString id;                 // "square2", "square1", "triangle", "noise"
    int entry = 0;
    QVector<SmbMusicEvent> events;
};

struct SmbMusicTimeline {
    QString header;
    int lengthBase = 0;
    int duration = 0;           // ticks, set by square 2
    QVector<SmbMusicChannel> channels;
};

// The SMB music bank: headers over one shared byte arena, the note period
// and note length tables. Songs share stream bytes, so an edit to one note
// can change others; the arena keeps its size, so edits are in place.
class SmbMusic {
public:
    static SmbMusic fromJson(const QJsonObject& resource);
    // The resource with the arena written back.
    QJsonObject toJson(QJsonObject resource) const;

    QVector<SmbMusicHeader> headers;
    QVector<int> arena;
    QVector<int> periods;
    QVector<int> lengths;
    QVector<QPair<QString, QString>> selectors;    // selector name, header
    QStringList groundSchedule;

    int indexOf(const QString& header) const;
    // Decodes one header the way the engine and the kit read it. Throws
    // SmbError for streams that run off the arena or have no terminator.
    SmbMusicTimeline timeline(const QString& header) const;
    // "ground_part_1 square 2, water triangle" for every header channel that
    // reads the byte.
    QStringList readersOf(int offset) const;

    // MIDI-style semitone of a period table entry on a channel, or -1.
    int pitchOf(int periodIndex, const QString& channel) const;
    // The period entry that sounds `pitch` on the channel, or -1.
    int periodFor(int pitch, const QString& channel) const;

    // In-place byte edits, each keeping the event's kind. Throw SmbError.
    void setNotePeriod(const QString& channel, int offset, int periodIndex);
    void setEventLength(const QString& channel, int offset, int lengthIndex);
    void setBeat(int offset, int beat);

    static QString channelLabel(const QString& channel);

private:
    mutable QHash<int, QStringList> readers_;
    mutable bool readersReady_ = false;
};

}  // namespace rl::smb
