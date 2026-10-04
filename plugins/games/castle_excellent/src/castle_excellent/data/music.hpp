#pragma once

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QVector>

namespace rl::castle {

// One note or rest a channel of a track set plays, and the MML that plays it.
struct CastleMusicNote {
    int start = 0;          // frames
    int length = 1;
    int pitch = -1;         // MIDI note; -1 for a rest
    int stream = 0;         // index into the document's streams
    int position = 0;       // of the note letter in the stream's text
    int span = 1;           // letter, accidental, length and dot
    int octave = 0;         // the octave offset in effect (semitones, mod 256)
};

// The music document (castle.mml-music.v1): 21 MML streams in a fixed arena
// and eight track sets that start three of them on the APU's two pulses and
// triangle. The engine (source reconstruction, audio_stream_engine.py) steps
// each channel once a frame; the tempo command is shared by all three.
class CastleMusic {
public:
    static constexpr int kChannels = 3;

    // What each channel plays in the first `frames` frames.
    static QVector<QVector<CastleMusicNote>> trace(const QJsonObject& music, int track, int frames);
    // The MIDI note the engine plays for a note letter ('a'-'g'), an
    // accidental ('+', '-' or 0) and an octave offset on a channel (0-2).
    static int pitchOf(const QJsonObject& music, char letter, char accidental, int octave, int channel);
    // Empty when the text is MML the engine and the kit accept.
    static QString checkText(const QString& text);
    // The arena bytes the streams take: text, terminator, restart pointer.
    static int usedBytes(const QJsonObject& music);
    // `music` with a stream's text replaced and used_bytes updated; throws
    // CastleError when the text is invalid or the arena overflows.
    static QJsonObject withText(const QJsonObject& music, int stream, const QString& text);
    // The MML that plays `note` at `pitch` instead: the letter and
    // accidental, wrapped in octave commands when the pitch is outside the
    // note's octave. Empty when no letter reaches it.
    static QString retune(const QJsonObject& music, const CastleMusicNote& note, int channel, int pitch,
                          const QString& text);
};

}  // namespace rl::castle
