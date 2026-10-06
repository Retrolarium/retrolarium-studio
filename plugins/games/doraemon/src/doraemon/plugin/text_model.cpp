#include "doraemon/plugin/text_model.hpp"

#include "doraemon/plugin/doraemon_session.hpp"
#include "sdk/i18n.hpp"

#include <QJsonArray>
#include <QPainter>

namespace rl::doraemon {

namespace {

// Letters every shell text can use: the font's capitals and digits, and
// the punctuation the game itself writes ("@" is the copyright sign).
bool allowed(QChar c) {
    return (c >= QLatin1Char('A') && c <= QLatin1Char('Z')) || (c >= QLatin1Char('0') && c <= QLatin1Char('9')) ||
           QStringLiteral(" .&@").contains(c);
}

QString titleLabel(const QString& id) {
    if (id == QLatin1String("push_start_button")) return rlTr("doraemon", "Title: start prompt");
    if (id == QLatin1String("hiscore")) return rlTr("doraemon", "Title: high score");
    if (id == QLatin1String("score")) return rlTr("doraemon", "Title: score");
    if (id == QLatin1String("hudson_copyright")) return rlTr("doraemon", "Title: Hudson copyright");
    if (id == QLatin1String("license_copyright")) return rlTr("doraemon", "Title: licence line");
    return rlTr("doraemon", "Title: %1").arg(id);
}

}  // namespace

DoraemonTextModel::DoraemonTextModel(DoraemonSession* session, bool credits)
    : TableModel(session), session_(session), credits_(credits) {
    readRows();
    connect(session_, &sdk::Session::contentChanged, this, &sdk::EditorModel::contentChanged);
}

void DoraemonTextModel::readRows() {
    const QJsonObject d = session_->text();
    rows_.clear();
    if (credits_) {
        const QJsonArray credits = d.value(QStringLiteral("ending_credit_rows")).toArray();
        for (int i = 0; i < credits.size(); ++i) {
            rows_ << Row{tr("Row %1").arg(i + 1, 3, 10, QLatin1Char('0')), QStringLiteral("credits"), i, 0,
                         credits.at(i).toString().size()};
        }
        return;
    }
    rows_ << Row{tr("GAME OVER"), QStringLiteral("game_over"), 0, 0, d.value(QStringLiteral("game_over")).toString().size()};
    const QJsonArray records = d.value(QStringLiteral("title_records")).toArray();
    for (int i = 0; i < records.size(); ++i) {
        const QJsonObject r = records.at(i).toObject();
        if (!r.contains(QStringLiteral("text"))) continue;
        rows_ << Row{titleLabel(r.value(QStringLiteral("id")).toString()), QStringLiteral("title"), i, 0,
                     r.value(QStringLiteral("text")).toString().size()};
    }
    const QJsonArray screens = d.value(QStringLiteral("chapter_help_screens")).toArray();
    for (int s = 0; s < screens.size(); ++s) {
        const QJsonArray segments = screens.at(s).toObject().value(QStringLiteral("segments")).toArray();
        for (int g = 0; g < segments.size(); ++g) {
            const QJsonObject seg = segments.at(g).toObject();
            if (!seg.contains(QStringLiteral("text"))) continue;
            rows_ << Row{tr("World %1 help: %2").arg(s + 1).arg(seg.value(QStringLiteral("id")).toString()),
                         QStringLiteral("help"), s, g, seg.value(QStringLiteral("text")).toString().size()};
        }
    }
}

QString DoraemonTextModel::title() const {
    return credits_ ? tr("Ending credits") : tr("Title, game over and help");
}

QString DoraemonTextModel::text(const QJsonObject& d, const Row& row) const {
    if (row.where == QLatin1String("game_over")) return d.value(QStringLiteral("game_over")).toString();
    if (row.where == QLatin1String("title")) {
        return d.value(QStringLiteral("title_records")).toArray().at(row.index).toObject().value(QStringLiteral("text")).toString();
    }
    if (row.where == QLatin1String("help")) {
        return d.value(QStringLiteral("chapter_help_screens")).toArray().at(row.index).toObject()
            .value(QStringLiteral("segments")).toArray().at(row.segment).toObject().value(QStringLiteral("text")).toString();
    }
    return d.value(QStringLiteral("ending_credit_rows")).toArray().at(row.index).toString();
}

QVector<sdk::TableColumn> DoraemonTextModel::columns() const {
    sdk::TableColumn where;
    where.key = QStringLiteral("where");
    where.label = credits_ ? tr("Row") : tr("Where");
    where.width = credits_ ? 80 : 220;
    where.muted = true;
    sdk::TableColumn text;
    text.key = QStringLiteral("text");
    text.label = tr("Text");
    text.monospace = true;
    text.editable = true;
    sdk::TableColumn places;
    places.key = QStringLiteral("places");
    places.label = tr("Places");
    places.width = 70;
    places.muted = true;
    return {where, text, places};
}

QString DoraemonTextModel::cell(int row, int column) const {
    if (row < 0 || row >= rows_.size()) return {};
    const Row& r = rows_.at(row);
    if (column == 0) return r.label;
    if (column == 1) {
        // Credits rows are centred with spaces; show them as they read.
        const QString t = text(session_->text(), r);
        return credits_ ? t.trimmed() : t;
    }
    return QString::number(r.width);
}

int DoraemonTextModel::maxLength(int row, int column) const {
    return column == 1 && row >= 0 && row < rows_.size() ? rows_.at(row).width : -1;
}

bool DoraemonTextModel::isRowDirty(int row) const {
    return row >= 0 && row < rows_.size() && text(session_->text(), rows_.at(row)) != text(session_->savedText(), rows_.at(row));
}

void DoraemonTextModel::setCurrentRow(int row) {
    if (row != row_ && row >= 0 && row < rows_.size()) {
        row_ = row;
        emit selectionChanged();
    }
}

bool DoraemonTextModel::setCell(int row, int column, const QString& value, QString* error) {
    if (column != 1 || row < 0 || row >= rows_.size()) return false;
    const Row& r = rows_.at(row);
    QString t = value.toUpper();
    for (const QChar c : t) {
        if (!allowed(c)) {
            if (error) *error = tr("The game writes A-Z, 0-9, space, '.', '&' and '@' (the copyright sign); not '%1'.").arg(c);
            return false;
        }
    }
    if (t.size() > r.width) {
        if (error) *error = tr("%1 keeps %2 places.").arg(r.label).arg(r.width);
        return false;
    }
    // Credits rows are centred (keeping the row's own indent when the new
    // text still fits it); other text is padded on the right.
    if (credits_) {
        const QString old = text(session_->text(), r);
        const int indent = old.size() - old.trimmed().size() > 0 ? int(old.indexOf(old.trimmed())) : 0;
        const int left = !old.trimmed().isEmpty() && indent + t.trimmed().size() <= r.width
                             ? indent
                             : (r.width - int(t.trimmed().size())) / 2;
        t = QString(left, QLatin1Char(' ')) + t.trimmed();
    }
    t = t.leftJustified(r.width, QLatin1Char(' '));
    QJsonObject d = session_->text();
    if (r.where == QLatin1String("game_over")) {
        d.insert(QStringLiteral("game_over"), t);
    } else if (r.where == QLatin1String("title")) {
        QJsonArray records = d.value(QStringLiteral("title_records")).toArray();
        QJsonObject record = records.at(r.index).toObject();
        record.insert(QStringLiteral("text"), t);
        records.replace(r.index, record);
        d.insert(QStringLiteral("title_records"), records);
    } else if (r.where == QLatin1String("help")) {
        QJsonArray screens = d.value(QStringLiteral("chapter_help_screens")).toArray();
        QJsonObject screen = screens.at(r.index).toObject();
        QJsonArray segments = screen.value(QStringLiteral("segments")).toArray();
        QJsonObject segment = segments.at(r.segment).toObject();
        segment.insert(QStringLiteral("text"), t);
        segments.replace(r.segment, segment);
        screen.insert(QStringLiteral("segments"), segments);
        screens.replace(r.index, screen);
        d.insert(QStringLiteral("chapter_help_screens"), screens);
    } else {
        QJsonArray rows = d.value(QStringLiteral("ending_credit_rows")).toArray();
        rows.replace(r.index, t);
        d.insert(QStringLiteral("ending_credit_rows"), rows);
    }
    return session_->editText(tr("Edit %1").arg(r.label), d, error);
}

QImage DoraemonTextModel::drawn(const QString& t, const Row& row) const {
    // The tiles the game puts on screen, in white on black: game over and
    // the title write blanks as $00 and periods as $5B; the credits
    // uploader maps space, period and ampersand to $7F, $5B and $5F; help
    // names are written as they are.
    QImage image(std::max(1, int(t.size())) * 8, 8, QImage::Format_ARGB32);
    image.fill(Qt::black);
    QPainter painter(&image);
    const DoraemonPainter& art = session_->painter();
    const QVector<int> grey{0x0f, 0x10, 0x00, 0x30};
    for (int i = 0; i < t.size(); ++i) {
        const QChar c = t.at(i);
        int tile = c.unicode() & 0xff;
        if (c == QLatin1Char(' ')) {
            if (row.where == QLatin1String("help")) tile = 0x20;
            else continue;
        }
        if (c == QLatin1Char('.') && row.where != QLatin1String("help")) tile = 0x5b;
        if (c == QLatin1Char('&') && row.where == QLatin1String("credits")) tile = 0x5f;
        painter.drawImage(i * 8, 0, art.shellTile(tile, grey));
    }
    painter.end();
    return image;
}

sdk::Inspection DoraemonTextModel::inspect() const {
    sdk::Inspection inspection;
    inspection.engineTag = QStringLiteral("doraemon.text@1");
    inspection.kicker = credits_ ? tr("Ending credits") : QStringLiteral("Text");
    if (row_ < 0 || row_ >= rows_.size()) return inspection;
    const Row& r = rows_.at(row_);
    const QString t = text(session_->text(), r);
    inspection.title = r.label;
    // The row in the game's own letters, one tile per place.
    const QImage line = drawn(t, r);
    inspection.library.grid = true;
    inspection.library.columns = 16;
    inspection.library.hint = tr("as the game draws it (CHR bank 3)");
    for (int i = 0; i < t.size(); ++i) {
        sdk::LibraryItem item;
        item.id = QStringLiteral("place:%1").arg(i);
        item.label = QString(t.at(i));
        item.icon = line.copy(i * 8, 0, 8, 8);
        inspection.library.items << item;
    }
    inspection.note = credits_
                          ? tr("One of the 380 rows the ending scrolls past, 32 places wide; text is centred. "
                               "The game writes A-Z, 0-9, '.', '&' and '@'.")
                          : tr("The text keeps its places; shorter text is padded with blanks. The game "
                               "writes A-Z, 0-9, '.', '&' and '@' (the copyright sign).");
    return inspection;
}

}  // namespace rl::doraemon
