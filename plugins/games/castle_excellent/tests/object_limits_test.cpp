#include "castle_excellent/data/object_limits.hpp"

#include <QJsonDocument>

#include <iostream>

using rl::castle_excellent::CastleExcellentObjectLimits;

namespace {
void expect(bool condition) {
    if (!condition) throw std::runtime_error("Unexpected Castle Excellent object limits");
}

QJsonObject profile() {
    return QJsonDocument::fromJson(R"({
      "id":"castle-excellent",
      "resources":[{"id":"objects","parameters":{
        "stream_cpu":61291,"stream_end_cpu":63740,"state_capacity":141,"maximum_per_room":24}}],
      "revisions":[{"id":"retail"},{"id":"expanded","resource_overrides":[{
        "id":"objects","parameters":{"stream_end_cpu":64060,"maximum_per_room":20}}]}]
    })")
        .object();
}
}  // namespace

int main() {
    try {
        auto p = profile();
        const auto retail = CastleExcellentObjectLimits::fromProfile(p, QStringLiteral("retail"));
        expect(retail.streamBytes == 2449 && retail.stateBytes == 141 && retail.perRoom == 24);
        const auto expanded = CastleExcellentObjectLimits::fromProfile(p, QStringLiteral("expanded"));
        expect(expanded.streamBytes == 2769 && expanded.stateBytes == 141 && expanded.perRoom == 20);
        auto revisions = p.value(QStringLiteral("revisions")).toArray();
        auto replacement = revisions[0].toObject();
        replacement.insert(QStringLiteral("resources"), QJsonDocument::fromJson(R"([
          {"id":"objects","parameters":{"stream_cpu":60000,"stream_end_cpu":63000,
           "state_capacity":140,"maximum_per_room":22}}])")
                                                            .array());
        revisions[0] = replacement;
        p.insert(QStringLiteral("revisions"), revisions);
        const auto replaced = CastleExcellentObjectLimits::fromProfile(p, QStringLiteral("retail"));
        expect(replaced.streamBytes == 3000 && replaced.stateBytes == 140 && replaced.perRoom == 22);
        for (const QByteArray& invalid : {QByteArray("-1"), QByteArray("1.5"), QByteArray("true"), QByteArray("25")}) {
            auto broken = QJsonDocument::fromJson(QJsonDocument(profile()).toJson().replace(
                                                      "\"maximum_per_room\": 24", "\"maximum_per_room\": " + invalid))
                              .object();
            bool rejected = false;
            try {
                CastleExcellentObjectLimits::fromProfile(broken, QStringLiteral("retail"));
            } catch (const std::exception&) {
                rejected = true;
            }
            expect(rejected);
        }
        bool rejected = false;
        try {
            CastleExcellentObjectLimits::fromProfile(p, QStringLiteral("missing"));
        } catch (const std::exception&) {
            rejected = true;
        }
        expect(rejected);
        std::cout << "Castle Excellent profile object limits: passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
