#include "gallery/theme_gallery.hpp"

#include "theme/icons.hpp"
#include "theme/paint.hpp"
#include "theme/theme.hpp"
#include "theme/typography.hpp"
#include "theme/window_frame.hpp"
#include "widgets/badge.hpp"
#include "widgets/budget_bar.hpp"
#include "widgets/collapsible_section.hpp"
#include "widgets/document_list.hpp"
#include "widgets/label.hpp"
#include "widgets/panel_tabs.hpp"
#include "widgets/primary_button.hpp"
#include "widgets/property_grid.hpp"
#include "widgets/rail_button.hpp"
#include "widgets/segmented_control.hpp"
#include "widgets/view_chip.hpp"

#include <QAction>
#include <QActionGroup>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QMenuBar>
#include <QPainter>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSlider>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVariant>

#include <array>
#include <tuple>
#include <utility>

namespace rl::gallery {

namespace paint = theme::paint;
using ui::Label;

namespace {

void repaintOnThemeChange(QWidget* widget) {
    QObject::connect(&theme::Theme::instance(), &theme::Theme::changed, widget,
                     [widget] { widget->update(); });
}

QFrame* surface(const char* name, QWidget* parent = nullptr) {
    auto* frame = new QFrame(parent);
    frame->setProperty("surface", QString::fromUtf8(name));
    frame->setFrameShape(QFrame::NoFrame);
    return frame;
}

QToolButton* iconButton(const QString& iconName, const QString& toolTip, int size = 30,
                        int glyph = 17) {
    auto* button = new QToolButton;
    button->setIcon(theme::icon(iconName));
    button->setIconSize(QSize(glyph, glyph));
    button->setToolTip(toolTip);
    button->setAutoRaise(true);
    button->setFixedSize(size, size);
    button->setCursor(Qt::PointingHandCursor);
    return button;
}

// A titled block of the gallery page.
QWidget* section(const QString& title, QWidget* content, const QString& note = QString()) {
    auto* box = new QWidget;
    auto* layout = new QVBoxLayout(box);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);
    auto* header = new QHBoxLayout;
    header->setSpacing(10);
    header->addWidget(new Label(title, Label::Role::Caps));
    if (!note.isEmpty()) {
        auto* hint = new Label(note, Label::Role::Meta);
        header->addWidget(hint);
    }
    header->addStretch(1);
    layout->addLayout(header);
    layout->addWidget(new ui::Divider(Qt::Horizontal));
    layout->addSpacing(4);
    layout->addWidget(content);
    return box;
}

// ---------------------------------------------------------------- tokens

class TokenSwatches final : public QWidget {
public:
    TokenSwatches() {
        setMinimumHeight(8 * 30);
        repaintOnThemeChange(this);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        const theme::Tokens& t = theme::current();
        const std::pair<const char*, QColor> entries[] = {
            {"bg", t.bg},           {"chrome", t.chrome},       {"panel", t.panel},
            {"field", t.field},     {"canvas", t.canvas},       {"text", t.text},
            {"muted", t.muted},     {"faint", t.faint},         {"line", t.line},
            {"lineStrong", t.lineStrong}, {"accent", t.accent}, {"accentInk", t.accentInk},
            {"accentSoft", t.accentSoft}, {"hover", t.hover},   {"onAccent", t.onAccent},
            {"mark", t.mark},
        };
        QPainter painter(this);
        const int columns = 2;
        const int cellWidth = width() / columns;
        int i = 0;
        for (const auto& entry : entries) {
            const int col = i % columns;
            const int row = i / columns;
            const QRect swatch(col * cellWidth, row * 30 + 3, 36, 22);
            // Translucent tokens are shown over the panel they sit on.
            painter.fillRect(swatch, t.panel);
            painter.fillRect(swatch, entry.second);
            paint::hairlineRect(painter, swatch, t.lineStrong);
            painter.setFont(theme::font(theme::TextRole::Body, 12.5));
            painter.setPen(t.text);
            painter.drawText(QRect(swatch.right() + 10, swatch.top(), 110, 22), Qt::AlignVCenter,
                             QString::fromUtf8(entry.first));
            painter.setFont(theme::font(theme::TextRole::Mono, 11.0));
            painter.setPen(t.muted);
            const QColor c = entry.second;
            const QString value = c.alpha() == 255
                                      ? c.name().toUpper()
                                      : QStringLiteral("%1 · %2%")
                                            .arg(c.name().toUpper())
                                            .arg(qRound(c.alphaF() * 100));
            painter.drawText(QRect(swatch.right() + 120, swatch.top(), cellWidth - 170, 22),
                             Qt::AlignVCenter, value);
            ++i;
        }
    }
};

QWidget* typographySample() {
    auto* box = new QWidget;
    auto* layout = new QVBoxLayout(box);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    layout->addWidget(new Label(QStringLiteral("Hero / start"), Label::Role::Title));
    layout->addWidget(new Label(QStringLiteral("Super Mario Bros."), Label::Role::Project));
    layout->addWidget(new Label(QStringLiteral("Tileset · Metatiles"), Label::Role::PanelTitle));
    layout->addWidget(new Label(QStringLiteral("Area object"), Label::Role::Caps));
    layout->addWidget(new Label(QStringLiteral("Where it's used"), Label::Role::CapsMuted));
    layout->addWidget(new Label(
        QStringLiteral("Drag on the map to move. Drag an edge to resize."), Label::Role::Body));
    layout->addWidget(new Label(QStringLiteral("Round 07 · layout 1120 / 1120 B"), Label::Role::Small));
    layout->addWidget(new Label(QStringLiteral("AreaData_1_1 · $9C8E · 2 bytes: pos + type"),
                                Label::Role::Mono));
    layout->addWidget(new Label(QStringLiteral("smb1.disasm.v2"), Label::Role::MonoSmall));
    auto* fonts = new Label(theme::fontsLoaded() ? QStringLiteral("Bundled fonts loaded")
                                                 : QStringLiteral("Fonts missing: using fallbacks"),
                            Label::Role::Meta);
    layout->addWidget(fonts);
    return box;
}

// ---------------------------------------------------------------- buttons

QWidget* buttonsSample() {
    auto* box = new QWidget;
    auto* layout = new QVBoxLayout(box);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(14);

    auto* icons = new QHBoxLayout;
    icons->setSpacing(2);
    for (const char* name : {"undo", "redo", "save", "revert"}) {
        icons->addWidget(iconButton(QString::fromUtf8(name), QString::fromUtf8(name)));
    }
    auto* disabled = iconButton(QStringLiteral("save"), QStringLiteral("Disabled"));
    disabled->setEnabled(false);
    icons->addWidget(disabled);
    icons->addSpacing(12);
    icons->addWidget(iconButton(QStringLiteral("plus"), QStringLiteral("New"), 22, 14));
    icons->addStretch(1);
    layout->addLayout(icons);

    auto* actions = new QHBoxLayout;
    actions->setSpacing(8);
    auto* build = new QPushButton(QStringLiteral("Build"));
    build->setProperty("size", QStringLiteral("large"));
    build->setFont(theme::font(theme::TextRole::Body, 12.5));
    actions->addWidget(build);
    auto* restore = new QPushButton(QStringLiteral("Remove unused"));
    restore->setFont(theme::font(theme::TextRole::Body, 12.0));
    actions->addWidget(restore);
    auto* off = new QPushButton(QStringLiteral("Unavailable"));
    off->setFont(theme::font(theme::TextRole::Body, 12.0));
    off->setEnabled(false);
    actions->addWidget(off);
    auto* dashed = new QPushButton(QStringLiteral("+"));
    dashed->setProperty("variant", QStringLiteral("dashed"));
    dashed->setFixedSize(46, 46);
    actions->addWidget(dashed);
    actions->addStretch(1);
    layout->addLayout(actions);

    auto* primaries = new QHBoxLayout;
    primaries->setSpacing(12);
    primaries->addWidget(new ui::PrimaryButton(QStringLiteral("Play from 1-1"), QStringLiteral("play")));
    auto* busy = new ui::PrimaryButton(QStringLiteral("Building…"));
    busy->setEnabled(false);
    primaries->addWidget(busy);
    primaries->addStretch(1);
    layout->addLayout(primaries);

    auto* badges = new QHBoxLayout;
    badges->setSpacing(8);
    badges->addWidget(new ui::Badge(QStringLiteral("NES"), ui::Badge::Kind::Platform));
    badges->addWidget(new ui::Badge(QStringLiteral("MD"), ui::Badge::Kind::Platform));
    badges->addWidget(new ui::Badge(QStringLiteral("JU"), ui::Badge::Kind::Tag));
    badges->addWidget(new ui::Badge(QStringLiteral("TileMapEditor"), ui::Badge::Kind::Code));
    badges->addWidget(new ui::Badge(QStringLiteral("4"), ui::Badge::Kind::Accent));
    badges->addStretch(1);
    layout->addLayout(badges);
    return box;
}

// ---------------------------------------------------------------- editor toolbar

QWidget* editorToolbarSample() {
    auto* box = new QWidget;
    auto* layout = new QVBoxLayout(box);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);

    auto* crumb = new QHBoxLayout;
    crumb->setSpacing(10);
    auto* kind = new Label(QStringLiteral("Levels"), Label::Role::Body);
    kind->setTone(QStringLiteral("muted"));
    crumb->addWidget(kind);
    auto* slash = new Label(QStringLiteral("/"), Label::Role::Body);
    slash->setTone(QStringLiteral("faint"));
    crumb->addWidget(slash);
    auto* doc = new Label(QStringLiteral("1-1"), Label::Role::Body);
    doc->setFont(theme::font(theme::TextRole::BodyMedium, theme::size::kBody));
    crumb->addWidget(doc);
    auto* edited = new Label(QStringLiteral("● edited"), Label::Role::Meta);
    edited->setTone(QStringLiteral("accent"));
    crumb->addWidget(edited);
    crumb->addWidget(new ui::Badge(QStringLiteral("TileMapEditor"), ui::Badge::Kind::Code));
    crumb->addStretch(1);
    auto* budgetText = new Label(QStringLiteral("area data 212 / 256 B"), Label::Role::Mono);
    budgetText->setFont(theme::font(theme::TextRole::Mono, 11.0));
    budgetText->setTone(QStringLiteral("muted"));
    crumb->addWidget(budgetText);
    auto* budget = new ui::BudgetBar;
    budget->setBudget(212, 256);
    crumb->addWidget(budget, 0, Qt::AlignVCenter);
    layout->addLayout(crumb);

    auto* row = new QHBoxLayout;
    row->setSpacing(8);
    auto* tools = new ui::SegmentedControl;
    tools->addIconSegment(QStringLiteral("select"), QStringLiteral("Select"));
    tools->addIconSegment(QStringLiteral("move"), QStringLiteral("Move"));
    tools->addIconSegment(QStringLiteral("place"), QStringLiteral("Place"));
    tools->addIconSegment(QStringLiteral("erase"), QStringLiteral("Erase"));
    row->addWidget(tools);
    auto* sep = new ui::Divider(Qt::Vertical);
    sep->setFixedHeight(18);
    row->addWidget(sep, 0, Qt::AlignVCenter);
    auto* layers = new ui::SegmentedControl;
    layers->addSegment(QStringLiteral("Foreground"));
    layers->addSegment(QStringLiteral("Background"));
    layers->addSegment(QStringLiteral("Objects"));
    row->addWidget(layers);
    auto* sep2 = new ui::Divider(Qt::Vertical);
    sep2->setFixedHeight(18);
    row->addWidget(sep2, 0, Qt::AlignVCenter);
    row->addWidget(new Label(QStringLiteral("View"), Label::Role::Caps));
    auto* palette = new ui::ViewChip(QStringLiteral("Palette"), QStringLiteral("Overworld"));
    palette->setChoices({QStringLiteral("Overworld"), QStringLiteral("Underground"),
                         QStringLiteral("Castle"), QStringLiteral("Underwater")});
    row->addWidget(palette);
    auto* grid = new ui::ViewChip(QStringLiteral("Grid"), QStringLiteral("On"));
    grid->setCheckable(true);
    grid->setChecked(true);
    QObject::connect(grid, &QAbstractButton::toggled, grid, [grid](bool on) {
        grid->setValue(on ? QStringLiteral("On") : QStringLiteral("Off"));
    });
    row->addWidget(grid);
    auto* zoom = new ui::ViewChip(QStringLiteral("Zoom"), QStringLiteral("2×"));
    zoom->setChoices({QStringLiteral("1×"), QStringLiteral("2×"), QStringLiteral("3×"),
                      QStringLiteral("4×")});
    row->addWidget(zoom);
    row->addStretch(1);
    layout->addLayout(row);

    auto* tileset = new QHBoxLayout;
    tileset->setSpacing(8);
    auto* levels = new ui::SegmentedControl;
    levels->addSegment(QStringLiteral("Metatiles"));
    levels->addSegment(QStringLiteral("Tiles"));
    tileset->addWidget(levels);
    auto* artCollision = new ui::SegmentedControl;
    artCollision->addSegment(QStringLiteral("Art"));
    artCollision->addSegment(QStringLiteral("Collision"));
    tileset->addWidget(artCollision);
    auto* pens = new ui::SegmentedControl;
    pens->addIconSegment(QStringLiteral("pencil"), QStringLiteral("Pencil"));
    pens->addIconSegment(QStringLiteral("fill"), QStringLiteral("Fill"));
    pens->addIconSegment(QStringLiteral("pick"), QStringLiteral("Pick"));
    tileset->addWidget(pens);
    tileset->addStretch(1);
    auto* back = new QPushButton(QStringLiteral("← Back to Palette 0 · terrain"));
    back->setFont(theme::font(theme::TextRole::Body, 12.0));
    tileset->addWidget(back);
    layout->addLayout(tileset);
    return box;
}

// ---------------------------------------------------------------- inspector

class LibraryItem final : public QWidget {
public:
    explicit LibraryItem(const QString& label) : label_(label) {
        setFixedHeight(34);
        setCursor(Qt::OpenHandCursor);
        setAttribute(Qt::WA_Hover);
        repaintOnThemeChange(this);
    }

protected:
    void enterEvent(QEvent*) override {
        hovered_ = true;
        update();
    }
    void leaveEvent(QEvent*) override {
        hovered_ = false;
        update();
    }
    void paintEvent(QPaintEvent*) override {
        const theme::Tokens& t = theme::current();
        QPainter painter(this);
        paint::hairlineRect(painter, rect(), hovered_ ? t.accent : t.line);
        const QRect glyph(7, (height() - 20) / 2, 20, 20);
        paint::hatch(painter, glyph, t.lineStrong, 5.0, 2.0);
        paint::hairlineRect(painter, glyph, t.lineStrong);
        painter.setFont(theme::font(theme::TextRole::Body, 12.0));
        painter.setPen(t.text);
        painter.drawText(rect().adjusted(34, 0, -6, 0), Qt::AlignVCenter,
                         painter.fontMetrics().elidedText(label_, Qt::ElideRight, width() - 40));
    }

private:
    QString label_;
    bool hovered_ = false;
};

QWidget* inspectorSample() {
    auto* panel = surface("panel");
    panel->setFixedWidth(300);
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* head = new QWidget;
    auto* headLayout = new QVBoxLayout(head);
    headLayout->setContentsMargins(14, 14, 14, 16);
    headLayout->setSpacing(10);
    headLayout->addWidget(new Label(QStringLiteral("Area object"), Label::Role::Caps));
    headLayout->addWidget(new Label(QStringLiteral("Hero / start"), Label::Role::Title));
    auto* grid = new ui::PropertyGrid;
    grid->addChoice(QStringLiteral("Type"),
                    {QStringLiteral("Hero / start"), QStringLiteral("? block"),
                     QStringLiteral("Brick row"), QStringLiteral("Pipe")});
    grid->addField(QStringLiteral("Position"), QStringLiteral("3, 12"), QStringLiteral("metatile"));
    grid->addField(QStringLiteral("Size"), QStringLiteral("1 × 1"));
    auto* page = grid->addField(QStringLiteral("Page"), QStringLiteral("0"));
    page->setMonospace(true);
    auto* locked = grid->addField(QStringLiteral("Record"), QStringLiteral("AreaData_1_1"));
    locked->setReadOnly(true);
    headLayout->addWidget(grid);
    auto* note = new Label(QStringLiteral("Drag on the map to move. Drag an edge to resize."),
                           Label::Role::Small);
    note->setWordWrap(true);
    headLayout->addWidget(note);
    layout->addWidget(head);
    layout->addWidget(new ui::Divider(Qt::Horizontal));

    auto* library = new QWidget;
    auto* libLayout = new QVBoxLayout(library);
    libLayout->setContentsMargins(14, 12, 14, 16);
    libLayout->setSpacing(8);
    auto* libHead = new QHBoxLayout;
    libHead->addWidget(new Label(QStringLiteral("Library"), Label::Role::Caps));
    libHead->addStretch(1);
    libHead->addWidget(new Label(QStringLiteral("drag onto the map"), Label::Role::Meta));
    libLayout->addLayout(libHead);
    auto* items = new QGridLayout;
    items->setSpacing(6);
    const char* names[] = {"Hero / start", "? block", "Brick row", "Pipe",
                           "Walker",        "Turtle", "Page jump", "Warp"};
    for (int i = 0; i < 8; ++i) {
        items->addWidget(new LibraryItem(QString::fromUtf8(names[i])), i / 2, i % 2);
    }
    libLayout->addLayout(items);
    layout->addWidget(library);
    layout->addWidget(new ui::Divider(Qt::Horizontal));

    auto* engine = new ui::EngineTable;
    engine->addRow(QStringLiteral("Record"), QStringLiteral("AreaData_1_1"));
    engine->addRow(QStringLiteral("Offset"), QStringLiteral("$9C8E + 4"));
    engine->addRow(QStringLiteral("Encoding"), QStringLiteral("2 bytes: pos + type"));
    auto* collapsible = new ui::CollapsibleSection(QStringLiteral("Engine data"), engine);
    collapsible->setTag(QStringLiteral("smb1.disasm.v2"));
    collapsible->setExpanded(true);
    layout->addWidget(collapsible);
    layout->addStretch(1);
    return panel;
}

// ---------------------------------------------------------------- document list

QWidget* documentListSample() {
    auto* panel = surface("panel");
    panel->setFixedWidth(228);
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* header = new QWidget;
    header->setFixedHeight(34);
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(12, 0, 6, 0);
    headerLayout->addWidget(new Label(QStringLiteral("Levels"), Label::Role::PanelTitle));
    headerLayout->addStretch(1);
    headerLayout->addWidget(iconButton(QStringLiteral("plus"), QStringLiteral("New"), 22, 14));
    layout->addWidget(header);
    layout->addWidget(new ui::Divider(Qt::Horizontal));

    auto* filterBox = new QWidget;
    auto* filterLayout = new QVBoxLayout(filterBox);
    filterLayout->setContentsMargins(8, 8, 8, 8);
    auto* filter = new QLineEdit;
    filter->setPlaceholderText(QStringLiteral("Filter"));
    filter->setFont(theme::font(theme::TextRole::Body, 12.0));
    filter->setFixedHeight(26);
    filter->addAction(theme::icon(QStringLiteral("search"), theme::IconTone::Faint),
                      QLineEdit::LeadingPosition);
    filter->setClearButtonEnabled(true);
    filterLayout->addWidget(filter);
    layout->addWidget(filterBox);

    auto* list = new ui::DocumentList;
    const char* types[] = {"overworld", "underground", "athletic", "castle"};
    for (int world = 1; world <= 3; ++world) {
        list->addGroup(QStringLiteral("World %1").arg(world));
        for (int level = 1; level <= 4; ++level) {
            const QString id = QStringLiteral("%1-%2").arg(world).arg(level);
            const bool water = world == 2 && level == 2;
            list->addDocument(id, id, QString::fromUtf8(water ? "water" : types[level - 1]),
                              id == QLatin1String("1-1") || id == QLatin1String("1-2"));
        }
    }
    list->selectDocument(QStringLiteral("1-1"));
    QObject::connect(filter, &QLineEdit::textChanged, list, &ui::DocumentList::setFilter);
    layout->addWidget(list, 1);
    return panel;
}

// ---------------------------------------------------------------- rail

QWidget* railSample() {
    auto* rail = surface("chrome");
    rail->setFixedWidth(62);
    auto* layout = new QVBoxLayout(rail);
    layout->setContentsMargins(0, 6, 0, 6);
    layout->setSpacing(2);
    const std::pair<const char*, const char*> kinds[] = {
        {"levels", "Levels"},   {"world", "World"},     {"tileset", "Tileset"},
        {"palettes", "Palettes"}, {"sprites", "Sprites"}, {"text", "Text"},
        {"sound", "Sound"},     {"playtest", "Playtest"},
    };
    bool first = true;
    for (const auto& kind : kinds) {
        auto* button = new ui::RailButton(QString::fromUtf8(kind.first),
                                          QString::fromUtf8(kind.second));
        button->setChecked(first);
        first = false;
        layout->addWidget(button);
    }
    layout->addStretch(1);
    return rail;
}

// ---------------------------------------------------------------- table

QWidget* tableSample() {
    auto* table = new QTableWidget(6, 3);
    table->setHorizontalHeaderLabels(
        {QStringLiteral("Name"), QStringLiteral("Game text"), QStringLiteral("Length")});
    QFont headerFont = theme::font(theme::TextRole::Caps, 12.0);
    table->horizontalHeader()->setFont(headerFont);
    table->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    table->horizontalHeader()->setHighlightSections(false);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    table->setColumnWidth(0, 200);
    table->setColumnWidth(2, 190);
    table->verticalHeader()->setVisible(false);
    table->verticalHeader()->setDefaultSectionSize(32);
    table->setShowGrid(false);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setMouseTracking(true);
    table->setFocusPolicy(Qt::NoFocus);

    const std::tuple<const char*, const char*, int, int> rows[] = {
        {"Status · world", "WORLD  TIME", 11, 11},
        {"Status · lives", "HERO", 5, 8},
        {"Top score", "TOP-", 4, 4},
        {"Thank you", "THANK YOU HERO!", 16, 16},
        {"Princess", "BUT OUR PRINCESS IS IN", 22, 24},
        {"Game over", "GAME OVER", 9, 12},
    };
    QFont mono = theme::font(theme::TextRole::Mono, 13.0);
    theme::setLetterSpacingEm(mono, 0.06);
    const QFont nameFont = theme::font(theme::TextRole::Body, 12.5);
    int row = 0;
    for (const auto& r : rows) {
        auto* name = new QTableWidgetItem(QString::fromUtf8(std::get<0>(r)));
        name->setFont(nameFont);
        name->setForeground(theme::current().muted);
        table->setItem(row, 0, name);
        auto* text = new QTableWidgetItem(QString::fromUtf8(std::get<1>(r)));
        text->setFont(mono);
        table->setItem(row, 1, text);

        auto* cell = new QWidget;
        auto* cellLayout = new QHBoxLayout(cell);
        cellLayout->setContentsMargins(14, 0, 14, 0);
        cellLayout->setSpacing(8);
        auto* bar = new ui::BudgetBar;
        bar->setBarThickness(3);
        bar->setMinimumWidth(40);
        bar->setMaximumWidth(QWIDGETSIZE_MAX);
        bar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        bar->setBudget(std::get<2>(r), std::get<3>(r));
        cellLayout->addWidget(bar, 1);
        auto* count = new Label(QStringLiteral("%1 / %2").arg(std::get<2>(r)).arg(std::get<3>(r)),
                                Label::Role::Mono);
        cellLayout->addWidget(count);
        table->setCellWidget(row, 2, cell);
        ++row;
    }
    table->selectRow(3);
    table->setFixedHeight(32 * 6 + 32);
    QObject::connect(&theme::Theme::instance(), &theme::Theme::changed, table, [table] {
        for (int r = 0; r < table->rowCount(); ++r) {
            table->item(r, 0)->setForeground(theme::current().muted);
        }
    });
    return table;
}

// ---------------------------------------------------------------- bottom panel

class SeverityMark final : public QWidget {
public:
    explicit SeverityMark(bool filled) : filled_(filled) {
        setFixedSize(7, 7);
        repaintOnThemeChange(this);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        const theme::Tokens& t = theme::current();
        QPainter painter(this);
        if (filled_) {
            painter.fillRect(rect(), t.accent);
        }
        paint::hairlineRect(painter, rect(), t.accent);
    }

private:
    bool filled_;
};

QWidget* messageRow(bool warning, const QString& source, const QString& message,
                    const QString& location) {
    auto* row = new QWidget;
    row->setFixedHeight(26);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(12, 0, 12, 0);
    layout->setSpacing(0);
    auto* sevBox = new QWidget;
    sevBox->setFixedWidth(84);
    auto* sevLayout = new QHBoxLayout(sevBox);
    sevLayout->setContentsMargins(0, 0, 0, 0);
    sevLayout->setSpacing(7);
    sevLayout->addWidget(new SeverityMark(warning));
    auto* sev = new Label(warning ? QStringLiteral("Warning") : QStringLiteral("Info"),
                          Label::Role::Row);
    sev->setTone(QStringLiteral("muted"));
    sevLayout->addWidget(sev);
    sevLayout->addStretch(1);
    layout->addWidget(sevBox);
    auto* src = new Label(source, Label::Role::Mono);
    src->setTone(QStringLiteral("muted"));
    src->setFixedWidth(160);
    layout->addWidget(src);
    layout->addWidget(new Label(message, Label::Role::Row), 1);
    auto* loc = new Label(location, Label::Role::Mono);
    loc->setTone(QStringLiteral("accent"));
    loc->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    loc->setFixedWidth(160);
    layout->addWidget(loc);
    return row;
}

QWidget* bottomPanelSample() {
    auto* panel = surface("panel");
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(new ui::Divider(Qt::Horizontal, ui::Divider::Strength::Strong));

    auto* strip = new QWidget;
    strip->setFixedHeight(28);
    auto* stripLayout = new QHBoxLayout(strip);
    stripLayout->setContentsMargins(6, 0, 10, 0);
    stripLayout->setSpacing(2);
    auto* chevron = iconButton(QStringLiteral("chevron-down"), QStringLiteral("Collapse"), 20, 10);
    chevron->setIcon(theme::icon(QStringLiteral("chevron-down"), theme::IconTone::Faint));
    stripLayout->addWidget(chevron);
    auto* tabs = new ui::PanelTabs;
    tabs->addTab(QStringLiteral("Messages"), QStringLiteral("3"));
    tabs->addTab(QStringLiteral("Build output"), QStringLiteral("4"));
    stripLayout->addWidget(tabs);
    stripLayout->addStretch(1);
    auto* summary = new Label(QStringLiteral("0 errors · 1 warning"), Label::Role::Small);
    summary->setTone(QStringLiteral("faint"));
    stripLayout->addWidget(summary);
    layout->addWidget(strip);
    layout->addWidget(new ui::Divider(Qt::Horizontal));

    auto* stack = new QWidget;
    auto* stackLayout = new QVBoxLayout(stack);
    stackLayout->setContentsMargins(0, 0, 0, 0);
    stackLayout->setSpacing(0);
    auto* messages = new QWidget;
    auto* msgLayout = new QVBoxLayout(messages);
    msgLayout->setContentsMargins(0, 0, 0, 0);
    msgLayout->setSpacing(0);
    const std::tuple<bool, const char*, const char*, const char*> rows[] = {
        {true, "levels/1-2", "Area data for 1-2 uses 250 of 256 bytes.", "1-2 · page 2"},
        {false, "text", "Status labels use all 11 bytes.", "Text · row 2"},
        {false, "build", "Build 14:02 verified, iNES header ok.", "build.log"},
    };
    for (const auto& r : rows) {
        msgLayout->addWidget(messageRow(std::get<0>(r), QString::fromUtf8(std::get<1>(r)),
                                        QString::fromUtf8(std::get<2>(r)),
                                        QString::fromUtf8(std::get<3>(r))));
        msgLayout->addWidget(new ui::Divider(Qt::Horizontal));
    }
    stackLayout->addWidget(messages);

    auto* log = new QPlainTextEdit;
    log->setProperty("log", true);
    log->setReadOnly(true);
    QFont logFont = theme::font(theme::TextRole::Mono, 11.5);
    log->setFont(logFont);
    log->setPlainText(QStringLiteral(
        "14:02:11  rltool studio build smb1.rlj\n"
        "14:02:12  assembled 40976 bytes, 0 warnings\n"
        "14:02:12  checksum fixed, iNES header ok\n"
        "14:02:12  verified against source image"));
    log->setVisible(false);
    stackLayout->addWidget(log);
    layout->addWidget(stack);
    panel->setFixedHeight(28 + 2 + 112);

    QObject::connect(tabs, &ui::PanelTabs::currentChanged, panel, [messages, log](int index) {
        messages->setVisible(index == 0);
        log->setVisible(index == 1);
    });
    QObject::connect(chevron, &QToolButton::clicked, panel, [panel, stack, chevron] {
        const bool open = !stack->isVisible();
        stack->setVisible(open);
        chevron->setIcon(theme::icon(open ? QStringLiteral("chevron-down")
                                          : QStringLiteral("chevron-right"),
                                     theme::IconTone::Faint));
        panel->setFixedHeight(open ? 28 + 2 + 112 : 28 + 2);
    });
    return panel;
}

// ---------------------------------------------------------------- canvases

// A stand-in map: the canvas ground, framing marks, page labels, object
// outlines and a selected object with its type tag. Pixels are placeholders.
class MapCanvasSample final : public QWidget {
public:
    MapCanvasSample() {
        setMinimumHeight(360);
        setCursor(Qt::CrossCursor);
        repaintOnThemeChange(this);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        const theme::Tokens& t = theme::current();
        QPainter painter(this);
        painter.fillRect(rect(), t.canvas);
        paint::dotGrid(painter, rect(), 16, theme::canvas::kGridDot);

        const int cell = 32;
        const int cols = 16;
        const int rows = 8;
        const QRect map((width() - cols * cell) / 2, 56, cols * cell, rows * cell);

        painter.fillRect(map, QColor(0x5c, 0x94, 0xfc));
        for (int x = 0; x < cols; ++x) {
            const QRect ground(map.left() + x * cell, map.bottom() - cell * 2 + 1, cell, cell * 2);
            painter.fillRect(ground, QColor(0xc8, 0x4c, 0x0c));
            painter.fillRect(ground.adjusted(2, 2, -cell / 2, -cell - 2), QColor(0xfc, 0x98, 0x38));
            painter.fillRect(ground.adjusted(cell / 2 + 2, cell + 2, -2, -2), QColor(0xfc, 0x98, 0x38));
        }
        const QRect pipe(map.left() + 12 * cell, map.bottom() - cell * 4 + 1, cell * 2, cell * 2);
        painter.fillRect(pipe, QColor(0x00, 0xa8, 0x00));
        painter.fillRect(pipe.adjusted(-4, 0, 4, -cell - 16), QColor(0x80, 0xd0, 0x10));
        const QRect block(map.left() + 6 * cell, map.top() + 2 * cell, cell, cell);
        painter.fillRect(block, QColor(0xfc, 0x98, 0x38));
        painter.fillRect(block.adjusted(10, 8, -10, -8), QColor(0x88, 0x14, 0x00));
        for (int x = 0; x <= cols; ++x) {
            painter.fillRect(QRect(map.left() + x * cell, map.top(), 1, map.height()),
                             QColor(0, 0, 0, 41));
        }
        for (int y = 0; y <= rows; ++y) {
            painter.fillRect(QRect(map.left(), map.top() + y * cell, map.width(), 1),
                             QColor(0, 0, 0, 41));
        }

        // Page separator and labels.
        QPen dashed(QColor(255, 255, 255, 140));
        dashed.setDashPattern({6, 5});
        painter.setPen(dashed);
        const int pageX = map.left() + 8 * cell;
        painter.drawLine(pageX, map.top(), pageX, map.bottom());
        painter.setFont(theme::font(theme::TextRole::Mono, 10.5));
        for (int page = 0; page < 2; ++page) {
            const int x = map.left() + page * 8 * cell;
            painter.fillRect(QRect(x, map.top() - 24, 1, 16), QColor(255, 255, 255, 90));
            painter.setPen(theme::canvas::kPageLabel);
            painter.drawText(QRect(x + 5, map.top() - 24, 80, 16), Qt::AlignVCenter,
                             QStringLiteral("Page %1").arg(page));
        }

        paint::cornerMarks(painter, map, 13, 7, theme::canvas::kMark);

        // An unselected object: dashed outline.
        QPen outline(theme::canvas::kObjectOutline);
        outline.setDashPattern({3, 3});
        painter.setPen(outline);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(block.adjusted(0, 0, -1, -1));
        painter.drawRect(pipe.adjusted(0, 0, -1, -1));

        // The selected object: fill, 2px border and type tag.
        const QRect hero(map.left() + 2 * cell, map.bottom() - cell * 3 + 1, cell, cell);
        painter.fillRect(hero.adjusted(8, 4, -8, 0), QColor(0xb8, 0x10, 0x00));
        painter.fillRect(hero, theme::canvas::kSelectionFill);
        paint::hairlineRect(painter, hero, theme::canvas::kSelection, 2.0);
        const QString tag = QStringLiteral("Hero / start");
        const QFont tagFont = theme::font(theme::TextRole::Mono, 10.0);
        painter.setFont(tagFont);
        const int tagWidth = painter.fontMetrics().horizontalAdvance(tag) + 10;
        const QRect tagRect(hero.left() - 2, hero.top() - 17, tagWidth, 14);
        painter.fillRect(tagRect, theme::canvas::kTagFill);
        painter.setPen(theme::canvas::kTagText);
        painter.drawText(tagRect, Qt::AlignCenter, tag);
    }
};

class CompositionSample final : public QWidget {
public:
    CompositionSample() {
        setFixedSize(320, 300);
        repaintOnThemeChange(this);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        const theme::Tokens& t = theme::current();
        QPainter painter(this);
        painter.fillRect(rect(), t.canvas);
        const QRect area(32, 22, 256, 256);
        paint::checkerboard(painter, area, 16, theme::canvas::kCheckerA, theme::canvas::kCheckerB);
        const QColor quadrant[4] = {QColor(0xfc, 0x98, 0x38), QColor(0xc8, 0x4c, 0x0c),
                                    QColor(0xc8, 0x4c, 0x0c), QColor(0xfc, 0x98, 0x38)};
        for (int i = 0; i < 4; ++i) {
            const QRect slot(area.left() + (i % 2) * 128, area.top() + (i / 2) * 128, 128, 128);
            painter.fillRect(slot.adjusted(8, 8, -8, -8), quadrant[i]);
            painter.setFont(theme::font(theme::TextRole::Mono, 10.0));
            const QString label = QStringLiteral("$%1").arg(0x24 + i, 2, 16, QLatin1Char('0')).toUpper();
            const QRect labelRect(slot.right() - 34, slot.top() + 4, 30, 14);
            painter.fillRect(labelRect, QColor(0, 0, 0, 153));
            painter.setPen(Qt::white);
            painter.drawText(labelRect, Qt::AlignCenter, label);
        }
        paint::hairlineRect(painter, QRect(area.left() + 128, area.top(), 128, 128),
                            theme::canvas::kSelection, 2.0);
        paint::cornerMarks(painter, area, 13, 8, theme::canvas::kMark, paint::kDiagonal);
    }
};

class PlaceholderSample final : public QWidget {
public:
    PlaceholderSample() {
        setFixedSize(320, 300);
        repaintOnThemeChange(this);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        const theme::Tokens& t = theme::current();
        QPainter painter(this);
        painter.fillRect(rect(), t.canvas);
        const QRect screen(28, 40, 264, 206);
        paint::hatch(painter, screen, QColor(255, 255, 255, 9), 20.0, 10.0);
        paint::hairlineRect(painter, screen, QColor(255, 255, 255, 38));
        paint::cornerMarks(painter, screen, 13, 7, theme::canvas::kMark, paint::kDiagonal);
        painter.setFont(theme::font(theme::TextRole::Mono, 11.5));
        painter.setPen(QColor(255, 255, 255, 140));
        painter.drawText(screen, Qt::AlignCenter,
                         QStringLiteral("embedded emulator\n256 × 240 · aspect-fit"));
    }
};

// Palette rows with value labels and a selected entry.
class SwatchSample final : public QWidget {
public:
    SwatchSample() {
        setFixedHeight(4 * 51 + 10);
        repaintOnThemeChange(this);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        const theme::Tokens& t = theme::current();
        QPainter painter(this);
        const std::pair<const char*, std::array<QRgb, 4>> rows[] = {
            {"BG 0", {0x5c94fc, 0x80d010, 0x00a800, 0x000000}},
            {"BG 1", {0x5c94fc, 0xfcfcfc, 0xfc9838, 0x881400}},
            {"SP 0", {0x5c94fc, 0xb81000, 0xfc9838, 0x886800}},
            {"SP 1", {0x5c94fc, 0x00a800, 0xfcfcfc, 0xfc9838}},
        };
        const int size = 46;
        for (int r = 0; r < 4; ++r) {
            const int y = 4 + r * (size + 5);
            painter.setFont(theme::font(theme::TextRole::Mono, 11.0));
            painter.setPen(t.muted);
            painter.drawText(QRect(0, y, 60, size), Qt::AlignVCenter,
                             QString::fromUtf8(rows[r].first));
            for (int i = 0; i < 4; ++i) {
                const QRect swatch(64 + i * (size + 4), y, size, size);
                painter.fillRect(swatch, QColor(rows[r].second[i]));
                paint::hairlineRect(painter, swatch, QColor(0, 0, 0, 77));
                painter.setFont(theme::font(theme::TextRole::Mono, 9.5));
                painter.setPen(Qt::white);
                painter.drawText(swatch.adjusted(0, 0, -3, -2), Qt::AlignRight | Qt::AlignBottom,
                                 QStringLiteral("%1").arg(0x21 + r * 4 + i, 2, 16, QLatin1Char('0')).toUpper());
                if (r == 1 && i == 2) {
                    paint::hairlineRect(painter, swatch.adjusted(-4, -4, 4, 4), t.accent, 2.0);
                }
            }
        }
    }
};

// ---------------------------------------------------------------- controls

QWidget* controlsSample() {
    auto* box = new QWidget;
    auto* layout = new QGridLayout(box);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setHorizontalSpacing(12);
    layout->setVerticalSpacing(10);
    const QFont body = theme::font(theme::TextRole::Body, 12.5);

    auto* edit = new QLineEdit(QStringLiteral("Round 07"));
    edit->setFont(body);
    edit->setFixedHeight(26);
    layout->addWidget(edit, 0, 0);
    auto* disabled = new QLineEdit(QStringLiteral("Disabled"));
    disabled->setFont(body);
    disabled->setFixedHeight(26);
    disabled->setEnabled(false);
    layout->addWidget(disabled, 0, 1);

    auto* combo = new QComboBox;
    combo->setFont(body);
    combo->setFixedHeight(26);
    combo->addItems({QStringLiteral("Duty 50% · env 3"), QStringLiteral("Duty 25% · env 1"),
                     QStringLiteral("Triangle")});
    layout->addWidget(combo, 1, 0);
    auto* hinted = new ui::PropertyField(QStringLiteral("5"), QStringLiteral("0–7"));
    layout->addWidget(hinted, 1, 1);

    auto* check = new QCheckBox(QStringLiteral("Show collision"));
    check->setFont(body);
    check->setChecked(true);
    layout->addWidget(check, 2, 0);
    auto* check2 = new QCheckBox(QStringLiteral("Keep length"));
    check2->setFont(body);
    layout->addWidget(check2, 2, 1);

    auto* radio = new QRadioButton(QStringLiteral("Solid"));
    radio->setFont(body);
    radio->setChecked(true);
    auto* radio2 = new QRadioButton(QStringLiteral("Top only"));
    radio2->setFont(body);
    layout->addWidget(radio, 3, 0);
    layout->addWidget(radio2, 3, 1);

    auto* slider = new QSlider(Qt::Horizontal);
    slider->setRange(0, 7);
    slider->setValue(5);
    layout->addWidget(slider, 4, 0);
    auto* progress = new QProgressBar;
    progress->setRange(0, 100);
    progress->setValue(62);
    progress->setTextVisible(false);
    layout->addWidget(progress, 4, 1, Qt::AlignVCenter);
    layout->setColumnStretch(0, 1);
    layout->setColumnStretch(1, 1);
    return box;
}

}  // namespace

ThemeGallery::ThemeGallery(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("Theme gallery — Retrolarium"));
    setMinimumSize(1280, 780);
    buildMenus();

    auto* root = surface("chrome");
    auto* layout = new QVBoxLayout(root);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(buildToolbar());
    layout->addWidget(new ui::Divider(Qt::Horizontal));
    layout->addWidget(buildPage(), 1);
    setCentralWidget(root);
}

void ThemeGallery::showEvent(QShowEvent* event) {
    QMainWindow::showEvent(event);
    theme::applyWindowFrame(this, theme::current());
}

void ThemeGallery::buildMenus() {
    QMenuBar* bar = menuBar();
    bar->setFont(theme::font(theme::TextRole::Body, theme::size::kMenu));
    for (const char* title : {"File", "Project", "Edit", "View", "Build", "Help"}) {
        QMenu* menu = bar->addMenu(QString::fromUtf8(title));
        menu->setFont(theme::font(theme::TextRole::Body, theme::size::kMenu));
        if (QLatin1String(title) == QLatin1String("View")) {
            QAction* dark = menu->addAction(QStringLiteral("Dark theme"));
            QAction* light = menu->addAction(QStringLiteral("Light theme"));
            dark->setCheckable(true);
            light->setCheckable(true);
            auto* group = new QActionGroup(menu);
            group->addAction(dark);
            group->addAction(light);
            (theme::Theme::instance().mode() == theme::Mode::Dark ? dark : light)->setChecked(true);
            connect(dark, &QAction::triggered, this,
                    [] { theme::Theme::instance().setMode(theme::Mode::Dark); });
            connect(light, &QAction::triggered, this,
                    [] { theme::Theme::instance().setMode(theme::Mode::Light); });
            connect(&theme::Theme::instance(), &theme::Theme::changed, menu,
                    [dark, light](theme::Mode mode) {
                        (mode == theme::Mode::Dark ? dark : light)->setChecked(true);
                    });
            menu->addSeparator();
            QAction* toggle = menu->addAction(QStringLiteral("Toggle theme"));
            toggle->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+L")));
            connect(toggle, &QAction::triggered, this, [] { theme::Theme::instance().toggle(); });
        } else if (QLatin1String(title) == QLatin1String("File")) {
            QAction* quit = menu->addAction(QStringLiteral("Exit"));
            quit->setShortcut(QKeySequence::Quit);
            connect(quit, &QAction::triggered, this, &QWidget::close);
        } else {
            menu->addAction(QStringLiteral("Nothing here yet"))->setEnabled(false);
        }
    }
}

QWidget* ThemeGallery::buildToolbar() {
    auto* bar = surface("chrome");
    bar->setFixedHeight(46);
    auto* layout = new QHBoxLayout(bar);
    layout->setContentsMargins(12, 0, 12, 0);
    layout->setSpacing(12);

    auto* project = new QPushButton;
    project->setFixedHeight(32);
    project->setCursor(Qt::PointingHandCursor);
    project->setToolTip(QStringLiteral("Switch project"));
    auto* projectLayout = new QHBoxLayout(project);
    projectLayout->setContentsMargins(6, 0, 10, 0);
    projectLayout->setSpacing(10);
    projectLayout->addWidget(new ui::Badge(QStringLiteral("NES"), ui::Badge::Kind::Platform));
    projectLayout->addWidget(new Label(QStringLiteral("Super Mario Bros."), Label::Role::Project));
    projectLayout->addWidget(new ui::Badge(QStringLiteral("JU"), ui::Badge::Kind::Tag));
    auto* chevron = new QLabel;
    chevron->setPixmap(theme::iconPixmap(QStringLiteral("chevron-down"), 10, theme::current().faint,
                                         devicePixelRatioF()));
    connect(&theme::Theme::instance(), &theme::Theme::changed, chevron, [chevron] {
        chevron->setPixmap(theme::iconPixmap(QStringLiteral("chevron-down"), 10,
                                             theme::current().faint, chevron->devicePixelRatioF()));
    });
    projectLayout->addWidget(chevron);
    project->setMinimumWidth(projectLayout->sizeHint().width());
    for (QWidget* child : project->findChildren<QWidget*>()) {
        child->setAttribute(Qt::WA_TransparentForMouseEvents);
    }
    layout->addWidget(project);

    auto* dirty = new QWidget;
    auto* dirtyLayout = new QHBoxLayout(dirty);
    dirtyLayout->setContentsMargins(0, 0, 0, 0);
    dirtyLayout->setSpacing(6);
    auto* mark = new SeverityMark(true);
    mark->setFixedSize(6, 6);
    dirtyLayout->addWidget(mark);
    dirtyLayout->addWidget(new Label(QStringLiteral("1 unsaved document"), Label::Role::Small));
    layout->addWidget(dirty);
    layout->addStretch(1);

    auto* actions = new QHBoxLayout;
    actions->setSpacing(2);
    actions->addWidget(iconButton(QStringLiteral("undo"), QStringLiteral("Undo")));
    actions->addWidget(iconButton(QStringLiteral("redo"), QStringLiteral("Redo")));
    actions->addWidget(iconButton(QStringLiteral("save"), QStringLiteral("Save all")));
    actions->addWidget(iconButton(QStringLiteral("revert"), QStringLiteral("Revert document to original")));
    auto* sep = new ui::Divider(Qt::Vertical);
    sep->setFixedHeight(18);
    actions->addSpacing(6);
    actions->addWidget(sep, 0, Qt::AlignVCenter);
    actions->addSpacing(6);
    auto* build = new QPushButton(QStringLiteral("Build"));
    build->setProperty("size", QStringLiteral("large"));
    build->setFont(theme::font(theme::TextRole::Body, 12.5));
    actions->addWidget(build);
    actions->addSpacing(4);
    actions->addWidget(new ui::PrimaryButton(QStringLiteral("Play from 1-1"), QStringLiteral("play")));
    actions->addSpacing(8);
    auto* themeButton = iconButton(theme::Theme::instance().mode() == theme::Mode::Dark
                                       ? QStringLiteral("sun")
                                       : QStringLiteral("moon"),
                                   QStringLiteral("Toggle theme (Ctrl+Shift+L)"));
    connect(themeButton, &QToolButton::clicked, this, [] { theme::Theme::instance().toggle(); });
    connect(&theme::Theme::instance(), &theme::Theme::changed, themeButton,
            [themeButton](theme::Mode mode) {
                themeButton->setIcon(theme::icon(
                    mode == theme::Mode::Dark ? QStringLiteral("sun") : QStringLiteral("moon")));
            });
    actions->addWidget(themeButton);
    layout->addLayout(actions);
    return bar;
}

QWidget* ThemeGallery::buildPage() {
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto* page = surface("bg");
    auto* layout = new QGridLayout(page);
    layout->setContentsMargins(28, 24, 28, 32);
    layout->setHorizontalSpacing(36);
    layout->setVerticalSpacing(32);

    layout->addWidget(section(QStringLiteral("Color tokens"), new TokenSwatches,
                              QStringLiteral("semantic set of the active theme")),
                      0, 0);
    layout->addWidget(section(QStringLiteral("Typography"), typographySample(),
                              QStringLiteral("Barlow · Barlow Condensed · IBM Plex Mono")),
                      0, 1);
    layout->addWidget(section(QStringLiteral("Buttons and badges"), buttonsSample()), 1, 0);
    layout->addWidget(section(QStringLiteral("Fields and controls"), controlsSample()), 1, 1);
    layout->addWidget(section(QStringLiteral("Editor header and toolbar"), editorToolbarSample(),
                              QStringLiteral("view chips never change data")),
                      2, 0, 1, 2);

    auto* shell = new QWidget;
    auto* shellLayout = new QHBoxLayout(shell);
    shellLayout->setContentsMargins(0, 0, 0, 0);
    shellLayout->setSpacing(0);
    shellLayout->addWidget(railSample());
    shellLayout->addWidget(new ui::Divider(Qt::Vertical));
    shellLayout->addWidget(documentListSample());
    shellLayout->addWidget(new ui::Divider(Qt::Vertical));
    auto* center = new QWidget;
    auto* centerLayout = new QVBoxLayout(center);
    centerLayout->setContentsMargins(0, 0, 0, 0);
    centerLayout->setSpacing(0);
    centerLayout->addWidget(new MapCanvasSample, 1);
    centerLayout->addWidget(bottomPanelSample());
    shellLayout->addWidget(center, 1);
    shellLayout->addWidget(new ui::Divider(Qt::Vertical));
    shellLayout->addWidget(inspectorSample());
    layout->addWidget(section(QStringLiteral("Shell panels"), shell,
                              QStringLiteral("rail · document list · canvas · inspector · messages")),
                      3, 0, 1, 2);

    auto* canvases = new QWidget;
    auto* canvasLayout = new QHBoxLayout(canvases);
    canvasLayout->setContentsMargins(0, 0, 0, 0);
    canvasLayout->setSpacing(16);
    canvasLayout->addWidget(new CompositionSample);
    canvasLayout->addWidget(new PlaceholderSample);
    auto* swatches = new SwatchSample;
    canvasLayout->addWidget(swatches, 1, Qt::AlignTop);
    layout->addWidget(section(QStringLiteral("Canvas grounds and swatches"), canvases,
                              QStringLiteral("these colors stay dark in both themes")),
                      4, 0, 1, 2);

    layout->addWidget(section(QStringLiteral("Table"), tableSample()), 5, 0, 1, 2);
    layout->setColumnStretch(0, 1);
    layout->setColumnStretch(1, 1);
    layout->setRowStretch(6, 1);

    scroll->setWidget(page);
    return scroll;
}

}  // namespace rl::gallery
