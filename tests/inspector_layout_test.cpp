// A ROM-free regression for the inspector's first painted frame, rebuilding
// and scrolling. Delaying until an eventual good layout would mask the bug.
#include "library_layout_checks.hpp"
#include "sdk/editor_model.hpp"
#include "shell/inspector_panel.hpp"

#include <QApplication>
#include <QScrollArea>
#include <QScrollBar>

#include <iostream>

namespace {
int failures = 0;

class LibraryModel final : public rl::sdk::EditorModel {
public:
    rl::sdk::EditorType type() const override { return rl::sdk::EditorType::TileMap; }
    QString title() const override { return QStringLiteral("Level"); }
    rl::sdk::Inspection inspect() const override {
        rl::sdk::Inspection result;
        result.kicker = QStringLiteral("AREA");
        result.title = heading;
        result.note = QStringLiteral(
            "Pick and drag objects into the level. The library must scroll instead of overlapping rows.");
        for (int i = 0; i < fields; ++i) {
            rl::sdk::Field field;
            field.key = QString::number(i);
            field.label = QStringLiteral("Property %1").arg(i);
            field.value = i;
            result.fields.append(field);
        }
        for (int i = 0; i < count; ++i) {
            rl::sdk::LibraryItem item;
            item.id = QString::number(i);
            item.label = QStringLiteral("Object %1").arg(i);
            result.library.items.append(item);
        }
        return result;
    }
    rl::sdk::Library sheet() const override { return patterns; }
    rl::sdk::Library patterns;

    void selectObject() {
        heading = QStringLiteral("Selected object");
        fields = 2;
        emit selectionChanged();
    }
    int count = 80;
    int fields = 6;
    QString heading = QStringLiteral("Ground 6");
};

void check(rl::shell::InspectorPanel& panel, const LibraryModel& model, const char* stage) {
    // grab() exercises the actual paint/layout path without waiting for timers.
    const QPixmap frame = panel.grab();
    if (frame.isNull()) {
        std::cerr << "FAIL: no inspector frame\n";
        ++failures;
    }
    QStringList ids;
    for (const auto& item : model.inspect().library.items) ids.append(item.id);
    const auto snapshot = rl::tests::inspectLibraryLayout(&panel, ids);
    if (!snapshot.error.isEmpty()) {
        std::cerr << "FAIL: " << stage << ": " << snapshot.error.toStdString() << '\n';
        ++failures;
    }
}
void checkSheetPane() {
    LibraryModel model;
    model.patterns.grid = true;
    model.patterns.columns = 16;
    model.patterns.cell = QSize(8, 16);
    for (int i = 0; i < 256; ++i) {
        rl::sdk::LibraryItem item;
        item.id = QString::number(i);
        model.patterns.items.append(item);
    }
    rl::editors::SheetPane pane(&model);
    for (const QSize size : {QSize(420, 400), QSize(240, 320), QSize(640, 800)}) {
        pane.resize(size);
        pane.show();
        QApplication::processEvents();
        pane.grab();
        auto* sheet =
            dynamic_cast<rl::editors::LibrarySheet*>(pane.findChild<QWidget*>(QStringLiteral("librarySheet")));
        auto* scroll = pane.findChild<QScrollArea*>(QStringLiteral("librarySheetScroll"));
        if (!sheet || !scroll) {
            std::cerr << "FAIL: the pattern sheet needs its own scroll viewport\n";
            ++failures;
            continue;
        }
        const auto snapshot = rl::tests::inspectLibrarySheet(sheet, model.patterns);
        if (!snapshot.error.isEmpty()) {
            std::cerr << "FAIL: real SheetPane clips tall sprite patterns\n";
            ++failures;
        }
        if (sheet->heightForWidth(sheet->width()) > scroll->viewport()->height() &&
            scroll->verticalScrollBar()->maximum() <= 0) {
            std::cerr << "FAIL: tall patterns need a vertical scroll range\n";
            ++failures;
        }
        scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
        pane.grab();
        const QPoint last = sheet->mapTo(scroll->viewport(), sheet->itemRect(255).center());
        if (!scroll->viewport()->rect().contains(last)) {
            std::cerr << "FAIL: the last sprite pattern is unreachable after scrolling\n";
            ++failures;
        }
    }
}

void checkPaintedGrid() {
    rl::sdk::Library expected;
    expected.grid = true;
    expected.columns = 16;
    for (int i = 0; i < 80; ++i) {
        rl::sdk::LibraryItem item;
        item.id = QString::number(i);
        expected.items.append(item);
    }
    rl::editors::LibrarySheet sheet;
    for (const QSize shape : {QSize(1, 1), QSize(1, 2), QSize(2, 1)}) {
        expected.cell = shape;
        sheet.setItems(expected.items, QString(), expected.columns, shape);
        sheet.resize(256, sheet.heightForWidth(256));
        if (!rl::tests::inspectLibrarySheet(&sheet, expected).error.isEmpty()) {
            std::cerr << "FAIL: a complete painted grid was rejected\n";
            ++failures;
        }
        // Vertical clipping is now prevented by the sheet's minimum height.
        // An impossibly narrow grid still exercises the verifier's bounds guard.
        sheet.resize(15, sheet.heightForWidth(15));
        if (rl::tests::inspectLibrarySheet(&sheet, expected).error.isEmpty()) {
            std::cerr << "FAIL: clipped library cells were accepted\n";
            ++failures;
        }
    }
    sheet.resize(256, sheet.heightForWidth(256));
    auto duplicate = expected;
    duplicate.items[1].id = duplicate.items[0].id;
    sheet.setItems(duplicate.items, QString(), duplicate.columns, duplicate.cell);
    if (rl::tests::inspectLibrarySheet(&sheet, duplicate).error.isEmpty()) {
        std::cerr << "FAIL: duplicate grid IDs were accepted\n";
        ++failures;
    }
    sheet.setItems(expected.items, QString(), expected.columns, expected.cell);
    auto stale = expected;
    stale.items.removeLast();
    if (rl::tests::inspectLibrarySheet(&sheet, stale).error.isEmpty()) {
        std::cerr << "FAIL: a stale grid was accepted\n";
        ++failures;
    }
}
}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    checkPaintedGrid();
    checkSheetPane();
    for (const QSize size : {QSize(300, 700), QSize(240, 420), QSize(420, 900)}) {
        LibraryModel model;
        rl::shell::InspectorPanel panel;
        panel.resize(size);
        panel.show();
        QApplication::processEvents();
        panel.setModel(&model);
        check(panel, model, "first frame after setModel");
        QApplication::processEvents();
        check(panel, model, "first event pass");
        auto* scroll = panel.findChild<QScrollArea*>();
        if (!scroll || scroll->verticalScrollBar()->maximum() <= 0) {
            std::cerr << "FAIL: the long library must have a vertical scroll range\n";
            ++failures;
        } else {
            scroll->verticalScrollBar()->setValue(200);
            const int position = scroll->verticalScrollBar()->value();
            model.selectObject();
            QApplication::processEvents();
            check(panel, model, "selection rebuild");
            if (scroll->verticalScrollBar()->value() != position) {
                std::cerr << "FAIL: rebuilding loses the library scroll position\n";
                ++failures;
            }
        }
        LibraryModel shortModel;
        shortModel.count = 3;
        panel.setModel(&shortModel);
        check(panel, shortModel, "switch to a short library");
        panel.showMessage(QStringLiteral("INFO"), QStringLiteral("No selection"), QStringLiteral("Choose an object."));
        if (!rl::tests::inspectLibraryLayout(&panel, {}).error.isEmpty()) {
            std::cerr << "FAIL: the message panel retains old cards\n";
            ++failures;
        }
        panel.setModel(nullptr);
    }
    if (!failures) std::cout << "OK: first frame, rebuild, scroll and model switch at three panel sizes\n";
    return failures ? 1 : 0;
}
