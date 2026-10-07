// The Flicky sound source document against a prepared Flicky project: a
// header can take another header's FM voice bank (the source then names a
// bank twice and still loads), and an edit put back leaves the source byte
// for byte as it was. Reads the project only.
//   flicky_sound_document_test <flicky project .rlj>

#include "flicky/data/sound_document.hpp"
#include "sdk/kit_session.hpp"
#include "sdk/project_file.hpp"

#include <QCoreApplication>
#include <QFileInfo>

#include <iostream>
#include <stdexcept>

using namespace rl::flicky;

namespace {

void expect(bool condition, const QString& message) {
    if (!condition) throw std::runtime_error(message.toStdString());
}

}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (argc != 2) {
        std::cerr << "usage: flicky_sound_document_test <flicky project .rlj>\n";
        return 2;
    }
    const QString path = QString::fromLocal8Bit(argv[1]);
    if (!QFileInfo::exists(path)) {
        std::cout << "skipped: no prepared Flicky project at " << path.toStdString() << "\n";
        return 0;
    }
    try {
        QString error;
        const rl::sdk::ProjectFile project = rl::sdk::ProjectFile::load(path, &error);
        expect(!project.path().isEmpty(), error);
        const QString document = rl::sdk::tabDocument(project, QStringLiteral("flicky.sound.v1"));
        expect(!document.isEmpty(), QStringLiteral("the project has no sound tab"));
        const FlickySoundDocument original = FlickySoundDocument::load(project.resolve(document));
        const QString source = original.source();
        const QStringList headers = original.headerIds();
        const QStringList banks = original.voiceBankIds();
        expect(headers.size() == 14 && banks.size() == 14, QStringLiteral("not 14 headers and 14 voice banks"));

        int checks = 0;
        for (const QString& id : headers) {
            const QString own = original.header(id).voices;
            for (const QString& bank : banks) {
                if (bank == own) continue;
                FlickySoundDocument edited = original;
                edited.setHeaderVoices(id, bank);
                expect(edited.header(id).voices == bank, QStringLiteral("%1 did not take %2").arg(id, bank));
                expect(edited.voiceBankIds() == banks, QStringLiteral("the bank list changed"));
                // Saved and opened again: two headers now share a bank.
                const FlickySoundDocument reopened = FlickySoundDocument::fromSource(edited.source());
                expect(reopened.header(id).voices == bank, QStringLiteral("%1 lost %2 on reopening").arg(id, bank));
                edited.setHeaderVoices(id, own);
                expect(edited.source() == source, QStringLiteral("%1 back to %2 is not the source").arg(id, own));
                ++checks;
            }
            FlickySoundDocument edited = original;
            const int scale = original.header(id).scale;
            edited.setHeaderValue(id, QStringLiteral("scale"), (scale + 1) & 0xff);
            expect(edited.header(id).scale == ((scale + 1) & 0xff), QStringLiteral("%1 scale not set").arg(id));
            edited.setHeaderValue(id, QStringLiteral("scale"), scale);
            expect(edited.source() == source, QStringLiteral("%1 scale back is not the source").arg(id));
            ++checks;
        }
        std::cout << checks << " header edits made and undone; " << banks.size() << " voice banks\n";
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
