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
// Runs this few tiles apart join one group; a group needs this many pictures
// (twice as many for 1 bit a pixel, which code and tables mimic most), this
// share of its tiles that are not blank, this share of all its tiles, and
// takes in up to this many blank tiles at either edge.
constexpr int kMergeGap = 16;
constexpr int kGroupPictures = 16;
constexpr double kGroupDensity = 0.6;
constexpr double kGroupFill = 0.3;
constexpr int kEdgeBlank = 2;

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

namespace {

// How much a tile's neighbouring pixels agree beyond chance: 0 random, 1 all
// agree; `empty` when the tile is one colour.
double likeness(const Format& format, const char* data, bool* empty) {
    const Pixels pixels = decode(format, data);
    const auto [low, high] = std::minmax_element(pixels.begin(), pixels.end());
    *empty = *low == *high;
    if (*empty) return 1.0;
    int agree = 0;
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            if (x < 7 && pixels[y * 8 + x] == pixels[y * 8 + x + 1]) ++agree;
            if (y < 7 && pixels[y * 8 + x] == pixels[y * 8 + x + 8]) ++agree;
        }
    }
    const double chance = 1.0 / double(1 << format.bits);
    return (agree / 112.0 - chance) / (1.0 - chance);
}

}  // namespace

Look look(const Format& format, const char* data) {
    bool empty = false;
    const double value = likeness(format, data, &empty);
    if (empty) return Look::Empty;
    return value >= kPictureLikeness ? Look::Picture : Look::Noise;
}

namespace {

QVector<Look> looks(const QByteArray& bytes, int from, const Format& format) {
    QVector<Look> all;
    for (int at = std::max(0, from); at + format.bytes <= bytes.size(); at += format.bytes) {
        all.append(look(format, bytes.constData() + at));
    }
    return all;
}

// The runs of pictures in tiles `looks` (first tile, last tile, pictures).
struct Run {
    int first = 0;
    int last = 0;
    int pictures = 0;
};

QVector<Run> runs(const QVector<Look>& looks) {
    QVector<Run> found;
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
        if (pictures >= kSectionPictures) found.append({first, last, pictures});
        i = last + 1;
    }
    return found;
}

// The byte shift within a tile at which tiles `first` to `last` read most
// like pictures (2-byte steps for 4 bits a pixel and deeper). Periodic
// pictures read almost as well a few rows off, so a shift must beat the
// tiles as they lie by this much.
constexpr double kShiftGain = 1.05;

int bestShift(const QByteArray& bytes, int from, const Format& format, int first, int last) {
    int best = 0;
    double bestScore = -1.0;
    for (int shift = 0; shift < format.bytes; shift += format.bits >= 4 ? 2 : 1) {
        double score = 0.0;
        for (int tile = first; tile <= last; ++tile) {
            const int at = from + tile * format.bytes + shift;
            if (at + format.bytes > bytes.size()) break;
            bool empty = false;
            const double value = likeness(format, bytes.constData() + at, &empty);
            score += empty ? 0.5 : value;
        }
        if (bestScore < 0.0 || score > bestScore * kShiftGain) {
            bestScore = score;
            best = shift;
        }
    }
    return best;
}

}  // namespace

QVector<Section> sections(const QByteArray& bytes, int from, const Format& format) {
    QVector<Section> found;
    for (const Run& run : runs(looks(bytes, from, format))) {
        found.append({std::max(0, from) + run.first * format.bytes, run.last - run.first + 1, run.pictures});
    }
    return found;
}

QVector<Group> groups(const QByteArray& bytes, int from, const QStringList& layouts) {
    from = std::max(0, from);
    struct Candidate {
        Group group;
        int bits = 0;
        double score = 0.0;
        int end() const { return group.offset + group.tiles * format(group.format).bytes; }
    };
    QVector<Candidate> candidates;
    for (const Format& candidate : formats()) {
        if (!layouts.isEmpty() && !layouts.contains(candidate.id)) continue;
        const QVector<Look> all = looks(bytes, from, candidate);
        QVector<Run> merged;
        for (const Run& run : runs(all)) {
            if (!merged.isEmpty() && run.first - merged.last().last - 1 <= kMergeGap) {
                merged.last().last = run.last;
                merged.last().pictures += run.pictures;
            } else {
                merged.append(run);
            }
        }
        for (Run run : merged) {
            const int total = run.last - run.first + 1;
            const int filled = int(std::count_if(all.begin() + run.first, all.begin() + run.last + 1,
                                                 [](Look look) { return look != Look::Empty; }));
            const int needed = candidate.bits == 1 ? 2 * kGroupPictures : kGroupPictures;
            if (run.pictures < needed || run.pictures < kGroupDensity * filled || run.pictures < kGroupFill * total) {
                continue;
            }
            // Blank tiles at the edges belong to the set (tile 0 is often blank).
            for (int k = 0; k < kEdgeBlank && run.first > 0 && all.at(run.first - 1) == Look::Empty; ++k) --run.first;
            for (int k = 0; k < kEdgeBlank && run.last + 1 < all.size() && all.at(run.last + 1) == Look::Empty; ++k) {
                ++run.last;
            }
            const int shift = bestShift(bytes, from, candidate, run.first, run.last);
            Candidate found;
            found.group.offset = from + run.first * candidate.bytes + shift;
            found.group.tiles = run.last - run.first + 1;
            if (found.group.offset + found.group.tiles * candidate.bytes > bytes.size()) --found.group.tiles;
            // Lined up anew, the tile across each edge may read as a picture.
            auto picture = [&](int at) {
                return at >= from && at + candidate.bytes <= bytes.size() &&
                       look(candidate, bytes.constData() + at) == Look::Picture;
            };
            while (picture(found.group.offset - candidate.bytes)) {
                found.group.offset -= candidate.bytes;
                ++found.group.tiles;
            }
            while (picture(found.group.offset + found.group.tiles * candidate.bytes)) ++found.group.tiles;
            found.group.format = candidate.id;
            found.group.pictures = run.pictures;
            found.bits = candidate.bits;
            found.score = prior(candidate) * double(run.pictures) * candidate.bytes;
            candidates.append(found);
        }
    }
    // Where layouts overlap the one covering more bytes in pictures stays, a
    // deeper one on a near tie when it spans the other.
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        return a.score != b.score ? a.score > b.score : a.bits > b.bits;
    });
    QVector<Candidate> chosen;
    for (const Candidate& candidate : candidates) {
        bool keep = true;
        for (int i = 0; i < chosen.size(); ++i) {
            const Candidate& other = chosen.at(i);
            if (candidate.group.offset >= other.end() || other.group.offset >= candidate.end()) continue;
            if (candidate.bits > other.bits && candidate.score >= kDeeperShare * other.score &&
                candidate.group.offset <= other.group.offset && other.end() <= candidate.end()) {
                chosen.removeAt(i);
                --i;
                continue;
            }
            keep = false;
            break;
        }
        if (keep) chosen.append(candidate);
    }
    std::sort(chosen.begin(), chosen.end(),
              [](const Candidate& a, const Candidate& b) { return a.group.offset < b.group.offset; });
    QVector<Group> found;
    for (const Candidate& candidate : chosen) {
        if (candidate.group.tiles > 0) found.append(candidate.group);
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
