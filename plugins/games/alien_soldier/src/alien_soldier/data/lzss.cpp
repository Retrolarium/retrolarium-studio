#include "alien_soldier/data/lzss.hpp"

#include <QHash>
#include <QVector>

#include <algorithm>

namespace rl::alien_soldier {

namespace {

constexpr int kMaxDistance = 0x400;
constexpr int kMinMatch = 2;
constexpr int kMaxMatch = 33;
constexpr int kMaxLiteral = 32;
constexpr int kMaxRepeat = 33;

}  // namespace

int lzssPayloadSize(const QByteArray& data) {
    const int size = int(data.size());
    if (size == 0) return 0;
    const auto byte = [&data](int at) { return quint8(data.at(at)); };
    // The longest match at each position, nearest source first.
    QHash<int, int> heads;
    QVector<int> previous(size, -1);
    QVector<int> matches(size, 0);
    for (int at = 0; at + 1 < size; ++at) {
        const int limit = std::min(kMaxMatch, size - at);
        const int key = (byte(at) << 8) | byte(at + 1);
        int best = 0;
        for (int source = heads.value(key, -1); source >= 0 && at - source <= kMaxDistance; source = previous[source]) {
            int length = 0;
            while (length < limit && byte(source + length) == byte(at + length)) ++length;
            if (length > best) {
                best = length;
                if (length == limit) break;
            }
        }
        if (best >= kMinMatch) matches[at] = best;
        previous[at] = heads.value(key, -1);
        heads.insert(key, at);
    }
    // The cheapest tokens from each position to the end.
    QVector<int> cost(size + 1, 0);
    for (int at = size - 1; at >= 0; --at) {
        const int left = size - at;
        int best = 2 + cost[at + 1];
        for (int length = 2; length <= std::min(kMaxLiteral, left); ++length)
            best = std::min(best, 1 + length + cost[at + length]);
        int run = 1;
        while (run < std::min(kMaxRepeat, left) && byte(at + run) == byte(at)) ++run;
        for (int length = kMinMatch; length <= run; ++length) best = std::min(best, 2 + cost[at + length]);
        if (left >= 4) {
            int pairs = 1;
            int alternating = 1;
            const int most = std::min(kMaxRepeat, left / 2);
            while (pairs < most && byte(at + 2 * pairs) == byte(at) && byte(at + 2 * pairs + 1) == byte(at + 1))
                ++pairs;
            while (alternating < most && byte(at + 2 * alternating) == byte(at)) ++alternating;
            for (int count = 2; count <= pairs; ++count) best = std::min(best, 3 + cost[at + 2 * count]);
            for (int count = 2; count <= alternating; ++count) best = std::min(best, 2 + count + cost[at + 2 * count]);
        }
        for (int take = kMinMatch; take <= matches[at]; ++take) best = std::min(best, 2 + cost[at + take]);
        cost[at] = best;
    }
    const int payload = 2 + cost[0];
    return payload + payload % 2;
}

}  // namespace rl::alien_soldier
