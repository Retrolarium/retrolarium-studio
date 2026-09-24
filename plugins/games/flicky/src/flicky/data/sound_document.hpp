#pragma once

#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

#include <array>
#include <stdexcept>

namespace retrolarium {

class FlickySoundDocumentError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct FlickySoundTrack final {
    QString type;
    QString sequence;
    int transpose = 0;
    int volume = 0;
    int flags = 0;
    int channel = 0;
    int pitchEnvelope = 0;
    int volumeEnvelope = 0;
};

struct FlickySoundHeader final {
    QString id;
    QString kind;
    QString voices;
    int scale = 0;
    int tempo = 0;
    QVector<FlickySoundTrack> tracks;
};

struct FlickySoundNote final {
    int offset = 0;
    int durationOffset = -1;
    int start = 0;
    int duration = 1;
    int note = -1; // -1 is a rest; 0..94 are pitched notes.
    bool sharedDuration = false;
};

struct FlickyFmOperator final {
    int detune = 0;
    int multiple = 0;
    int rateScale = 0;
    int attack = 0;
    int amplitudeModulation = 0;
    int decay1 = 0;
    int decay2 = 0;
    int sustain = 0;
    int release = 0;
    int totalLevel = 0;
};

struct FlickyFmVoice final {
    int algorithm = 0;
    int feedback = 0;
    std::array<FlickyFmOperator, 4> operators {};
};

// The native source document edits numeric tokens in the canonical Z80 ASM.
// Untouched text remains byte-for-byte identical to the imported baseline.
class FlickySoundDocument final {
public:
    static FlickySoundDocument load(const QString &path);
    static FlickySoundDocument fromSource(const QString &source);

    [[nodiscard]] QString source() const;
    void save(const QString &path) const;
    [[nodiscard]] QStringList headerIds() const;
    [[nodiscard]] FlickySoundHeader header(const QString &id) const;
    [[nodiscard]] QStringList sequenceIds() const;
    [[nodiscard]] QVector<int> sequenceBytes(const QString &id) const;
    [[nodiscard]] QVector<FlickySoundNote> notes(const QString &id) const;
    [[nodiscard]] QStringList voiceBankIds() const;
    [[nodiscard]] int voiceCount(const QString &bank) const;
    [[nodiscard]] FlickyFmVoice voice(const QString &bank, int index) const;
    [[nodiscard]] QVector<int> priorityEnvelopeBytes() const;

    void setHeaderValue(const QString &id, const QString &field, int value);
    void setHeaderVoices(const QString &id, const QString &bank);
    void setTrackValue(const QString &id, int track, const QString &field,
                       int value);
    void setTrackSequence(const QString &id, int track, const QString &sequence);
    void setSequenceByte(const QString &id, int offset, int value);
    void setVoice(const QString &bank, int index, const FlickyFmVoice &voice);
    void setPriorityEnvelopeByte(int offset, int value);

    [[nodiscard]] QString blockText(const QString &id) const;
    [[nodiscard]] QString baselineBlockText(const QString &id) const;
    void replaceBlock(const QString &id, const QString &replacement);
    void restoreBlock(const QString &id);

private:
    struct Span final { int start = 0; int length = 0; };
    struct ByteBlock final {
        QVector<int> bytes;
        QVector<Span> tokens;
    };
    struct TrackRecord final {
        FlickySoundTrack value;
        QMap<QString, Span> spans;
    };
    struct HeaderRecord final {
        FlickySoundHeader value;
        QMap<QString, Span> spans;
        QVector<TrackRecord> tracks;
    };
    struct Parsed final {
        QMap<QString, Span> blocks;
        QMap<QString, HeaderRecord> headers;
        QMap<QString, ByteBlock> byteBlocks;
        QStringList headerOrder;
        QStringList sequenceOrder;
        QStringList voiceOrder;
    };

    static Parsed parse(const QString &source);
    void replaceSpan(Span span, const QString &text);
    void replaceByteTokens(const QString &id,
                           const QVector<int> &replacement);
    void replaceText(Span span, const QString &replacement);
    void checkShape(const Parsed &candidate) const;
    [[nodiscard]] const ByteBlock &byteBlock(const QString &id) const;

    QString source_;
    QString baseline_;
    Parsed parsed_;
    Parsed baselineParsed_;
};

} // namespace retrolarium
