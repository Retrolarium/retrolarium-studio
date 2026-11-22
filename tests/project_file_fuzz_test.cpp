// The project file reader against hand-edited and damaged .rlj files: a
// fixed seed of mutations (characters replaced, cut, repeated; the text cut
// short; deep nesting) of a typical project. Every one either parses or is
// refused with an error; none may crash or hang. Deterministic, so a failure
// repeats. Needs no project.
//   project_file_fuzz_test

#include "sdk/project_file.hpp"

#include <QCoreApplication>
#include <QString>
#include <QStringList>

#include <iostream>

namespace {

int failures = 0;

const char* const kSeed =
    "# A project as the kit writes it.\n"
    "schema = \"retrolarium.project\"\n"
    "schema_version = 1\n"
    "\n"
    "[project]\n"
    "id = \"game-jp\"\n"
    "name = \"Game \\\"JP\\\" \\\\ test\"\n"
    "platform = 'nes'\n"
    "profile = \"game.v1\"\n"
    "\n"
    "[source]\n"
    "rom = \"Game (Japan).nes\"\n"
    "sha256 = \"0123456789abcdef\"\n"
    "size = 0x10010\n"
    "ratio = 1.5\n"
    "flags = [1, 2, [3, 4], \"five\"]\n"
    "enabled = true\n"
    "\n"
    "[[tabs]]\n"
    "kind = \"levels\"\n"
    "documents = [\"a\", \"b\"]\n"
    "\n"
    "[[tabs]]\n"
    "kind = \"tiles\"\n"
    "[tabs.view]\n"
    "zoom = 4\n"
    "\n"
    "[build.output]\n"
    "rom = \"build/game.nes\"  # trailing comment\n";

// A small deterministic generator (a linear congruential one): the same
// mutations on every system.
class Random {
public:
    explicit Random(quint32 seed) : state_(seed) {}
    quint32 next() {
        state_ = state_ * 1664525u + 1013904223u;
        return state_ >> 8;
    }
    int below(int n) { return n <= 0 ? 0 : static_cast<int>(next() % static_cast<quint32>(n)); }

private:
    quint32 state_;
};

QString mutate(const QString& seed, Random& random) {
    static const QString special = QStringLiteral("[]=\"'#\n.,{}\\x0 -+eE_");
    QString text = seed;
    const int edits = 1 + random.below(4);
    for (int e = 0; e < edits && !text.isEmpty(); ++e) {
        const int at = random.below(text.size());
        switch (random.below(5)) {
            case 0:
                text[at] = special.at(random.below(special.size()));
                break;
            case 1:
                text.remove(at, 1 + random.below(12));
                break;
            case 2:
                text.insert(at, text.mid(random.below(text.size()), 1 + random.below(24)));
                break;
            case 3:
                text.truncate(at);
                break;
            default:
                text.insert(at, special.at(random.below(special.size())));
                break;
        }
    }
    return text;
}

void parse(const QString& text, const char* what) {
    QString error;
    const rl::sdk::ProjectFile project = rl::sdk::ProjectFile::parse(text, QStringLiteral("fuzz.rlj"), &error);
    if (!project.isValid() && error.isEmpty()) {
        std::cerr << "FAIL: " << what << " was refused without a reason\n";
        ++failures;
    }
}

}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const QString seed = QString::fromUtf8(kSeed);
    QString error;
    if (!rl::sdk::ProjectFile::parse(seed, QStringLiteral("seed.rlj"), &error).isValid()) {
        std::cerr << "FAIL: the seed project does not parse: " << error.toStdString() << '\n';
        return 1;
    }

    Random random(0x52455452u);
    int refused = 0;
    for (int i = 0; i < 100; ++i) {
        const QString text = mutate(seed, random);
        QString why;
        if (!rl::sdk::ProjectFile::parse(text, QStringLiteral("fuzz.rlj"), &why).isValid()) {
            ++refused;
            if (why.isEmpty()) {
                std::cerr << "FAIL: mutation " << i << " was refused without a reason\n";
                ++failures;
            }
        }
    }

    // Nesting far past the reader's limits: refused, not a stack overflow.
    parse(QStringLiteral("a = ") + QString(100000, QLatin1Char('[')), "an array nested 100000 deep");
    QStringList keys;
    for (int i = 0; i < 5000; ++i) keys << QStringLiteral("t");
    parse(QStringLiteral("[") + keys.join(QLatin1Char('.')) + QStringLiteral("]\n"), "a table header 5000 keys deep");
    parse(QString(), "an empty file");
    parse(QStringLiteral("\"unterminated"), "an unterminated string");
    parse(QStringLiteral("key = \"\\u"), "a cut escape");

    if (failures != 0) return 1;
    std::cout << "project file fuzz: 100 mutations (" << refused << " refused) and deep nesting hold\n";
    return 0;
}
