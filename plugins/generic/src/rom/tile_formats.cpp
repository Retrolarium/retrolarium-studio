#include "rom/tile_formats.hpp"

#include <algorithm>

namespace rl::rom::tiles {

namespace {

// A tile reads as a picture when its neighbouring pixels agree this much
// more often than random pixels of its depth would (0 random, 1 all agree).
constexpr double kPictureLikeness = 0.45;
// A section goes on over this many noisy tiles in a row (dithering, a stray
// tile) and needs this many pictures to count.
constexpr int kNoiseGap = 3;
constexpr int kSectionPictures = 8;
// Below this share of the bytes in sections no format is chosen.
constexpr double kDetectShare = 0.02;

int bit(const char* data, int index, int shift) {
    return (quint8(data[index]) >> shift) & 1;
}

// Mega Drive and Game Boy Advance tiles differ only in which nibble is the
// left pixel, so they often score alike: the rarer one loses a close call.
double prior(const Format& format) {
    return format.layout == Layout::Gba ? 0.95 : 1.0;
}

// Deeper tiles read in a shallower layout still look like pictures (a SNES
// 4bpp tile is two Game Boy tiles side by side), shallower ones in a deeper
// layout do not: a deeper layout this close to the best wins.
constexpr double kDeeperShare = 0.95;

}  // namespace

const QVector<Format>& formats() {
    static const QVector<Format> all{
        {QStringLiteral("1bpp"), Layout::OneBit, 1, 8},     {QStringLiteral("nes"), Layout::Nes, 2, 16},
        {QStringLiteral("gb"), Layout::GameBoy, 2, 16},     {QStringLiteral("snes4"), Layout::Snes4, 4, 32},
        {QStringLiteral("md"), Layout::MegaDrive, 4, 32},   {QStringLiteral("gba"), Layout::Gba, 4, 32},
        {QStringLiteral("8bpp"), Layout::EightBit, 8, 64},
    };
    return all;
}

const Format& format(const QString& id) {
    for (const Format& candidate : formats()) {
        if (candidate.id == id) return candidate;
    }
    return formats().at(1);
}

Pixels decode(const Format& format, const char* data) {
    Pixels pixels{};
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            const int shift = 7 - x;
            int pen = 0;
            switch (format.layout) {
            case Layout::OneBit: pen = bit(data, y, shift); break;
            case Layout::Nes: pen = bit(data, y, shift) | bit(data, 8 + y, shift) << 1; break;
            case Layout::GameBoy: pen = bit(data, 2 * y, shift) | bit(data, 2 * y + 1, shift) << 1; break;
            case Layout::Snes4:
                pen = bit(data, 2 * y, shift) | bit(data, 2 * y + 1, shift) << 1 | bit(data, 16 + 2 * y, shift) << 2 |
                      bit(data, 17 + 2 * y, shift) << 3;
                break;
            case Layout::MegaDrive:
            case Layout::Gba: {
                const quint8 byte = quint8(data[y * 4 + x / 2]);
                const bool high = (x % 2 == 0) == (format.layout == Layout::MegaDrive);
                pen = high ? byte >> 4 : byte & 0x0f;
                break;
            }
            case Layout::EightBit: pen = quint8(data[y * 8 + x]); break;
            }
            pixels[y * 8 + x] = quint8(pen);
        }
    }
    return pixels;
}

void encode(const Format& format, const Pixels& pixels, char* data) {
    std::fill(data, data + format.bytes, '\0');
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            const int pen = pixels[y * 8 + x];
            const int shift = 7 - x;
            auto set = [&](int index, int plane) {
                data[index] = char(quint8(data[index]) | ((pen >> plane) & 1) << shift);
            };
            switch (format.layout) {
            case Layout::OneBit: set(y, 0); break;
            case Layout::Nes:
                set(y, 0);
                set(8 + y, 1);
                break;
            case Layout::GameBoy:
                set(2 * y, 0);
                set(2 * y + 1, 1);
                break;
            case Layout::Snes4:
                set(2 * y, 0);
                set(2 * y + 1, 1);
                set(16 + 2 * y, 2);
                set(17 + 2 * y, 3);
                break;
            case Layout::MegaDrive:
            case Layout::Gba: {
                const bool high = (x % 2 == 0) == (format.layout == Layout::MegaDrive);
                char& byte = data[y * 4 + x / 2];
                byte = char(quint8(byte) | (high ? (pen & 0x0f) << 4 : pen & 0x0f));
                break;
            }
            case Layout::EightBit: data[y * 8 + x] = char(pen); break;
            }
        }
    }
}

Look look(const Format& format, const char* data) {
    const Pixels pixels = decode(format, data);
    const auto [low, high] = std::minmax_element(pixels.begin(), pixels.end());
    if (*low == *high) return Look::Empty;
    int agree = 0;
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            if (x < 7 && pixels[y * 8 + x] == pixels[y * 8 + x + 1]) ++agree;
            if (y < 7 && pixels[y * 8 + x] == pixels[y * 8 + x + 8]) ++agree;
        }
    }
    const double chance = 1.0 / double(1 << format.bits);
    const double likeness = (agree / 112.0 - chance) / (1.0 - chance);
    return likeness >= kPictureLikeness ? Look::Picture : Look::Noise;
}

QVector<Section> sections(const QByteArray& bytes, int from, const Format& format) {
    QVector<Look> looks;
    for (int at = std::max(0, from); at + format.bytes <= bytes.size(); at += format.bytes) {
        looks.append(look(format, bytes.constData() + at));
    }
    QVector<Section> found;
    int i = 0;
    while (i < looks.size()) {
        if (looks.at(i) != Look::Picture) {
            ++i;
            continue;
        }
        const int first = i;
        int last = i;
        int pictures = 0;
        int noise = 0;
        for (int j = i; j < looks.size(); ++j) {
            if (looks.at(j) == Look::Picture) {
                ++pictures;
                last = j;
                noise = 0;
            } else if (looks.at(j) == Look::Noise && ++noise > kNoiseGap) {
                break;
            }
        }
        if (pictures >= kSectionPictures) {
            found.append({std::max(0, from) + first * format.bytes, last - first + 1, pictures});
        }
        i = last + 1;
    }
    return found;
}

QString detect(const QByteArray& bytes, int from) {
    const int length = bytes.size() - std::max(0, from);
    if (length <= 0) return {};
    QVector<double> scores;
    int best = -1;
    for (const Format& candidate : formats()) {
        qint64 covered = 0;
        for (const Section& section : sections(bytes, from, candidate)) covered += qint64(section.pictures) * candidate.bytes;
        scores.append(prior(candidate) * double(covered) / double(length));
        if (best < 0 || scores.last() > scores.at(best)) best = scores.size() - 1;
    }
    if (best < 0 || scores.at(best) < kDetectShare) return {};
    const int top = best;
    for (int i = 0; i < formats().size(); ++i) {
        if (formats().at(i).bits > formats().at(best).bits && scores.at(i) >= kDeeperShare * scores.at(top)) best = i;
    }
    return formats().at(best).id;
}

}  // namespace rl::rom::tiles
