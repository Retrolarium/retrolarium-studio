// The project file reader against hand-edited and damaged .rlj files: a
// fixed seed of mutations (characters replaced, cut, repeated; the text cut
// short; deep nesting) of a typical project. Every one either parses or is
// refused with an error; none may crash or hang. Deterministic, so a failure
// repeats. Needs no project.
//   project_file_fuzz_test

#include "sdk/project_file.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#ifdef Q_OS_WIN
#include <windows.h>
#include <winioctl.h>
#else
#include <unistd.h>
#endif

#include <iostream>
#include <algorithm>
#include <string>

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

bool directoryLink(const QString& link, const QString& target) {
#ifdef Q_OS_WIN
    // A junction needs no symlink privilege or Developer Mode on Windows.
    const std::wstring nativeLink = QDir::toNativeSeparators(link).toStdWString();
    const std::wstring nativeTarget = QDir::toNativeSeparators(target).toStdWString();
    const std::wstring substitute = L"\\??\\" + nativeTarget;
    struct MountPoint {
        DWORD tag = IO_REPARSE_TAG_MOUNT_POINT;
        WORD length = 0, reserved = 0;
        WORD substituteOffset = 0, substituteLength = 0, printOffset = 0, printLength = 0;
        wchar_t names[2048]{};
    } data;
    if (substitute.size() + nativeTarget.size() + 2 >= 2048) return false;
    data.substituteLength = static_cast<WORD>(substitute.size() * sizeof(wchar_t));
    data.printOffset = data.substituteLength + sizeof(wchar_t);
    data.printLength = static_cast<WORD>(nativeTarget.size() * sizeof(wchar_t));
    data.length = static_cast<WORD>(8 + data.printOffset + data.printLength + sizeof(wchar_t));
    std::copy(substitute.begin(), substitute.end(), data.names);
    std::copy(nativeTarget.begin(), nativeTarget.end(), data.names + data.printOffset / sizeof(wchar_t));
    if (!CreateDirectoryW(nativeLink.c_str(), nullptr)) return false;
    const HANDLE handle = CreateFileW(nativeLink.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                      FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    DWORD returned = 0;
    const bool ok =
        handle != INVALID_HANDLE_VALUE &&
        DeviceIoControl(handle, FSCTL_SET_REPARSE_POINT, &data, 8 + data.length, nullptr, 0, &returned, nullptr) != 0;
    if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
    if (!ok) RemoveDirectoryW(nativeLink.c_str());
    return ok;
#else
    return ::symlink(target.toUtf8().constData(), link.toUtf8().constData()) == 0;
#endif
}

void repositoryPathBoundaries() {
    QTemporaryDir folder;
    const QString games = folder.path() + QStringLiteral("/games");
    const QString game = games + QStringLiteral("/game");
    const QString revision = game + QStringLiteral("/.retrolarium/revision");
    QDir().mkpath(revision);
    QDir().mkpath(games + QStringLiteral("/other"));
    const QString path = revision + QStringLiteral("/project.rlj");
    const auto expect = [&](const QString& document, bool allowed) {
        const QString text = QStringLiteral("[source]\nrepository = '../..'\n[[workspace.tabs]]\ndocument = '") +
                             document + QStringLiteral("'\n");
        QString why;
        const auto project = rl::sdk::ProjectFile::parse(text, path, &why);
        if (project.isValid() != allowed || (!allowed && why.isEmpty())) {
            std::cerr << "FAIL: repository boundary: " << document.toStdString() << ": " << why.toStdString() << '\n';
            ++failures;
        }
    };
    expect(QStringLiteral("../../data/levels.json"), true);
    expect(QStringLiteral("../../../other/levels.json"), false);
    expect(QStringLiteral("../../../../outside.json"), false);

    // Ordinary projects retain their own directory as the boundary.
    const QString ordinaryPath = folder.path() + QStringLiteral("/ordinary/project.rlj");
    QDir().mkpath(QFileInfo(ordinaryPath).absolutePath());
    QString why;
    const auto ordinary = rl::sdk::ProjectFile::parse(
        QStringLiteral("[[workspace.tabs]]\ndocument = 'data/levels.json'\n"), ordinaryPath, &why);
    if (!ordinary.isValid()) {
        std::cerr << "FAIL: ordinary project after repository boundary checks: " << why.toStdString() << '\n';
        ++failures;
    }
}

void portableNames() {
    QTemporaryDir folder;
    const QString path = folder.path() + QStringLiteral("/project.rlj");
    QString why;
    const auto project = rl::sdk::ProjectFile::parse(QString(), path, &why);
    if (!project.isValid()) {
        std::cerr << "FAIL: portable-name fixture: " << why.toStdString() << '\n';
        ++failures;
        return;
    }
    const auto expect = [&](const QString& relative, bool allowed) {
        bool resolved = true;
        try {
            project.resolve(relative);
        } catch (const std::exception& exception) {
            resolved = false;
            why = QString::fromUtf8(exception.what());
        }
        if (resolved != allowed || (!allowed && why.isEmpty())) {
            std::cerr << "FAIL: portable file name: " << relative.toStdString() << '\n';
            ++failures;
        }
    };
    for (const QString& relative :
         {QStringLiteral("docs./a.json"), QStringLiteral("docs /a.json"), QStringLiteral("file."),
          QStringLiteral("file "), QStringLiteral("nul.json"), QStringLiteral("Com1"), QStringLiteral("lpt9.txt"),
          QStringLiteral("CONIN$"), QStringLiteral("conout$.json")}) {
        expect(relative, false);
    }
    for (const QString& relative : {QStringLiteral("console.json"), QStringLiteral("nullable.json"),
                                    QStringLiteral("com10.json"), QStringLiteral("data.normal/file.json")}) {
        expect(relative, true);
    }
}

void projectWrites() {
    QTemporaryDir folder;
    const QString root = folder.path() + QStringLiteral("/project");
    QDir().mkpath(root);
    QString why;
    const auto project = rl::sdk::ProjectFile::parse(QString(), root + QStringLiteral("/project.rlj"), &why);
    const QString document = root + QStringLiteral("/nested/documents/levels.json");
    if (!project.isValid() || !project.writeFile(document, QByteArray("new contents"), &why)) {
        std::cerr << "FAIL: nested project write: " << why.toStdString() << '\n';
        ++failures;
    }
    QFile saved(document);
    if (!saved.open(QIODevice::ReadOnly) || saved.readAll() != QByteArray("new contents")) {
        std::cerr << "FAIL: saved project contents differ\n";
        ++failures;
    }

    const QString outside = folder.path() + QStringLiteral("/outside");
    QDir().mkpath(outside);
    const QString link = root + QStringLiteral("/build");
    if (directoryLink(link, outside)) {
        const QString redirected = link + QStringLiteral("/demo-steps.json");
        if (project.writeFile(redirected, QByteArray("must stay inside"), &why) ||
            QFileInfo::exists(outside + QStringLiteral("/demo-steps.json"))) {
            std::cerr << "FAIL: redirected project write reached an external directory\n";
            ++failures;
        }
    } else {
        std::cerr << "SKIP: this filesystem cannot create directory links for the write probe\n";
    }
}

void pathBoundaries() {
    QTemporaryDir folder;
    const QString root = folder.path() + QStringLiteral("/project");
    QDir().mkpath(root);
    const QString path = root + QStringLiteral("/project.rlj");
    const auto expect = [&](const QString& document, bool allowed, const QString& source = QString()) {
        const QString text =
            source + QStringLiteral("\n[[workspace.tabs]]\ndocument = '") + document + QStringLiteral("'\n");
        QString why;
        const auto project = rl::sdk::ProjectFile::parse(text, path, &why);
        if (project.isValid() != allowed || (!allowed && why.isEmpty())) {
            std::cerr << "FAIL: document boundary: " << document.toStdString() << '\n';
            ++failures;
        }
    };
    expect(QStringLiteral("data/new.json"), true);
    for (const QString& outside :
         {QStringLiteral("../outside.json"), QStringLiteral("/outside.json"), QStringLiteral("C:/outside.json"),
          QStringLiteral("\\\\server\\share\\a.json"), QStringLiteral("data/../../outside.json")}) {
        expect(outside, false);
    }
    // Project metadata cannot expand the root to an arbitrary ancestor.
    expect(QStringLiteral("../outside.json"), false, QStringLiteral("[source]\nrepository = '../..'\n"));
    const QString revision = root + QStringLiteral("/.retrolarium/base");
    QDir().mkpath(revision);
    QDir().mkpath(root + QStringLiteral("/hack/base"));
    QString why;
    const QString generated = QStringLiteral(
        "[source]\nrepository = '../..'\n[[workspace.tabs]]\ndocument = '../../hack/base/levels.json'\n");
    const auto project = rl::sdk::ProjectFile::parse(generated, revision + QStringLiteral("/project.rlj"), &why);
    if (!project.isValid()) {
        std::cerr << "FAIL: normal generated ROM project: " << why.toStdString() << '\n';
        ++failures;
    }
    QDir().mkpath(folder.path() + QStringLiteral("/outside"));
    const QString link = root + QStringLiteral("/linked");
    const QString target = folder.path() + QStringLiteral("/outside");
    const bool linked = directoryLink(link, target);
    if (linked) {
        expect(QStringLiteral("linked/new.json"), false);
        const auto noTabs = rl::sdk::ProjectFile::parse(QString(), path, &why);
        if (noTabs.validateWritePath(link + QStringLiteral("/new.json"), &why) ||
            noTabs.validateWritePath(link + QStringLiteral("/../new.json"), &why) ||
            !noTabs.validateWritePath(root + QStringLiteral("/new.json"), &why)) {
            std::cerr << "FAIL: actual write paths must be checked even without matching tab metadata\n";
            ++failures;
        }
        // An opened project's path is rechecked before save, too.
        const QString text = QStringLiteral("[[workspace.tabs]]\ndocument = 'later/data.json'\n");
        const auto opened = rl::sdk::ProjectFile::parse(text, path, &why);
        const QString later = root + QStringLiteral("/later");
        if (!directoryLink(later, target)) {
            std::cerr << "FAIL: cannot create the second test directory link\n";
            ++failures;
        }
        if (!opened.isValid() || opened.validateEditablePaths(&why)) {
            std::cerr << "FAIL: changed link escaped before save\n";
            ++failures;
        }
        std::cout << "project paths: directory links and actual write boundaries checked\n";
    } else {
        std::cerr << "SKIP: this filesystem cannot create directory links for the boundary probe\n";
    }
}

}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    repositoryPathBoundaries();
    portableNames();
    pathBoundaries();
    projectWrites();
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
