// The NES record table editor (kit nes.record-table.v1) on an in-memory
// bundle: numbers and hexadecimal in range, "—" for a field that may be
// empty, choices by their label, locked cells left alone, and a unique field
// that swaps with the record holding the value. Needs no project.
//   nes_record_table_test

#include "nes/bundle.hpp"
#include "nes/editors.hpp"
#include "nes/record_table_model.hpp"
#include "sdk/session.hpp"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>

#include <iostream>
#include <memory>
#include <stdexcept>

using namespace rl;

namespace {

void expect(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

const char* const kBundle = R"({"resources": [{
    "id": "table", "kind": "objects", "codec": "nes.record-table.v1",
    "record_labels": ["Room 1", "Room 2", "Room 3"],
    "fields": [
        {"name": "x", "label": "X", "minimum": 0, "maximum": 8, "signed": false, "none": 255, "locked": [0]},
        {"name": "plays", "label": "Plays", "minimum": 0, "maximum": 255, "signed": false, "unique": true,
         "choices": [{"value": 1, "label": "One"}, {"value": 4, "label": "Four"}, {"value": 7, "label": "Seven"}]}
    ],
    "records": [{"x": 2, "plays": 1}, {"x": null, "plays": 4}, {"x": 8, "plays": 7}]
}]})";

class TestSession final : public sdk::Session, public nes::BundleOwner {
public:
    TestSession() : bundle_(nes::Bundle::fromJson(QJsonDocument::fromJson(kBundle).object())), saved_(bundle_) {}

    sdk::Profile profile() const override { return {}; }
    QVector<sdk::Kind> kinds() const override { return {}; }
    QVector<sdk::DocumentGroup> documents(const QString&) const override { return {}; }
    std::unique_ptr<sdk::EditorModel> openEditor(const QString&, const QString&) override { return nullptr; }
    bool save(QString*) override { return true; }

    sdk::Session* bundleSession() override { return this; }
    const nes::Bundle& bundle() const override { return bundle_; }
    const nes::Bundle& savedBundle() const override { return saved_; }
    bool editBundle(const QString&, const std::function<void(nes::Bundle&)>& change, QString*) override {
        change(bundle_);
        emit contentChanged();
        return true;
    }
    QVector<nes::PreviewPalette> previewPalettes(const QString&, bool) const override { return {}; }

private:
    nes::Bundle bundle_;
    nes::Bundle saved_;
};

QJsonObject record(const TestSession& session, int row) {
    return session.bundle()
        .resource(QStringLiteral("table"))
        .value(QStringLiteral("records"))
        .toArray()
        .at(row)
        .toObject();
}

}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        TestSession session;
        nes::RecordTableModel model(&session, QStringLiteral("table"), QStringLiteral("Rooms"));
        expect(model.rowCount() == 3 && model.columns().size() == 3, "three rows of two fields and the label");
        expect(model.cell(1, 0) == QLatin1String("Room 2"), "record label");
        expect(model.cell(1, 1) == QStringLiteral("—"), "an empty field shows a dash");
        expect(model.cell(2, 2) == QLatin1String("Seven"), "a choice shows its label");
        QString error;
        expect(model.setCell(1, 1, QStringLiteral("$5"), &error) && record(session, 1).value("x").toInt() == 5,
               "hexadecimal input");
        expect(!model.setCell(1, 1, QStringLiteral("9"), &error), "out of range is refused");
        expect(model.setCell(1, 1, QStringLiteral("—"), &error) && record(session, 1).value("x").isNull(),
               "a dash empties the field");
        expect(!model.setCell(0, 1, QStringLiteral("3"), &error) && record(session, 0).value("x").toInt() == 2,
               "a locked cell stays");
        expect(model.cellChoices(0, 2).size() == 3 && model.cellChoices(0, 1).isEmpty(), "choices only where given");
        expect(model.setCell(0, 2, QStringLiteral("Seven"), &error), "picking a taken value swaps");
        expect(record(session, 0).value("plays").toInt() == 7 && record(session, 2).value("plays").toInt() == 1,
               "the other record takes the old value");
        expect(model.isRowDirty(0) && !model.isRowDirty(1), "dirty rows follow the saved bundle");
        std::cout << "nes record table: edits, empty fields, locks and swaps\n";
    } catch (const std::exception& e) {
        std::cerr << "FAILED: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
