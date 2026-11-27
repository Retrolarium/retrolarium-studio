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
}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
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
