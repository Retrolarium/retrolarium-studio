#include "flicky/plugin/semantics_models.hpp"

#include "flicky/plugin/flicky_session.hpp"
#include "flicky/plugin/objects.hpp"
#include "megadrive/vdp.hpp"

#include <algorithm>

namespace rl::flicky {

namespace {

QString hex(int value, int digits) {
    return QStringLiteral("$%1").arg(value, digits, 16, QLatin1Char('0')).toUpper();
}

QString two(int value) {
    return QStringLiteral("%1").arg(value, 2, 10, QLatin1Char('0'));
}

// Trailing number of an id such as Level_Palette7.
int suffix(const QString& id, const QString& prefix) {
    if (!id.startsWith(prefix)) {
        return -1;
    }
    bool ok = false;
    const int value = id.mid(prefix.size()).toInt(&ok);
    return ok ? value : -1;
}

constexpr int kMaxSamples = 8;

}  // namespace

// ---------------------------------------------------------------- palettes

SemanticsPaletteModel::SemanticsPaletteModel(FlickySession* session, const QString& id)
    : PaletteModel(session), session_(session), id_(id) {
    connect(session_, &FlickySession::semanticsChanged, this, &sdk::EditorModel::contentChanged);
    connect(session_, &FlickySession::graphicsChanged, this, &sdk::EditorModel::contentChanged);
}

int SemanticsPaletteModel::cramBase() const {
    // Where the scene renderer loads the palette (game RAM layout).
    if (id_.startsWith(QLatin1String("Level_AccentPalette"))) return 60;
    if (id_.startsWith(QLatin1String("Level_Palette"))) return 16;
    return -1;
}

QVector<int> SemanticsPaletteModel::rounds() const {
    // Round themes, as the game picks them for a one-based round number.
    QVector<int> result;
    const int level = suffix(id_, QStringLiteral("Level_Palette"));
    const int accent = suffix(id_, QStringLiteral("Level_AccentPalette"));
    for (int round = 0; round < objects::kRounds; ++round) {
        const int number = round + 1;
        if ((level >= 0 && (number % 48) / 4 == level) || (accent >= 0 && (number - 1) % 15 == accent)) {
            result.append(round);
        }
    }
    return result;
}

QVector<sdk::PaletteRow> SemanticsPaletteModel::rows() const {
    sdk::PaletteRow row;
    const int base = cramBase();
    row.label = base >= 0 ? tr("CRAM %1").arg(base) : tr("Colors");
    const QVector<int> colours = session_->semantics().paletteColours(id_);
    for (int i = 0; i < colours.size(); ++i) {
        row.colors.append(QColor::fromRgb(megadrive::cramToArgb(colours.at(i))));
        row.labels.append(QString::number(i, 16).toUpper());
    }
    return {row};
}

void SemanticsPaletteModel::setCurrentEntry(QPoint entry) {
    const int count = session_->semantics().paletteColours(id_).size();
    const int index = std::clamp(entry.x(), 0, std::max(0, count - 1));
    if (index != entry_) {
        entry_ = index;
        emit selectionChanged();
    }
}

QString SemanticsPaletteModel::pickerTitle() const {
    return tr("9-bit RGB · 0–7 per channel");
}

QStringList SemanticsPaletteModel::channelNames() const {
    return {tr("Red"), tr("Green"), tr("Blue")};
}

QVector<int> SemanticsPaletteModel::currentChannels() const {
    const int word = session_->semantics().colour(id_, entry_);
    return {megadrive::cramRed(word), megadrive::cramGreen(word), megadrive::cramBlue(word)};
}

bool SemanticsPaletteModel::setChannel(int channel, int level, QString* error) {
    QVector<int> rgb = currentChannels();
    if (channel < 0 || channel > 2) {
        return false;
    }
    rgb[channel] = level;
    const int word = megadrive::cramWord(rgb[0], rgb[1], rgb[2]);
    const QString id = id_;
    const int entry = entry_;
    return session_->editSemantics(tr("Edit %1 color %2").arg(id).arg(entry),
                                   [id, entry, word](FlickySemanticsDocument& s) { s.setColour(id, entry, word); },
                                   error);
}

QVector<sdk::PaletteSample> SemanticsPaletteModel::samples() const {
    QVector<sdk::PaletteSample> result;
    if (id_ == QLatin1String("Title_LogoPalette")) {
        // Line 3 of the title screen: the logo.
        sdk::PaletteSample sample;
        sample.label = tr("Title");
        sample.caption = tr("Title screen");
        try {
            sample.image = session_->renderer().renderScreen(FlickyLevelSceneRenderer::Screen::Title);
        } catch (const std::exception&) {
        }
        result.append(sample);
        return result;
    }
    for (int round : rounds()) {
        if (objects::isBonusRound(round)) {
            continue;
        }
        sdk::PaletteSample sample;
        sample.label = tr("Round %1").arg(two(round + 1));
        sample.caption = tr("Round %1 scene").arg(two(round + 1));
        sample.image = session_->roundPreview(round);
        result.append(sample);
        if (result.size() == kMaxSamples) {
            break;
        }
    }
    return result;
}

sdk::Inspection SemanticsPaletteModel::inspect() const {
    const int word = session_->semantics().colour(id_, entry_);
    sdk::Inspection inspection;
    inspection.kicker = tr("Color");
    inspection.title = QStringLiteral("%1 · %2").arg(id_).arg(QString::number(entry_, 16).toUpper());
    sdk::Field value;
    value.key = QStringLiteral("value");
    value.label = tr("Value");
    value.value = hex(word, 3);
    value.monospace = true;
    const QVector<int> rgb = currentChannels();
    sdk::Field channels;
    channels.key = QStringLiteral("rgb");
    channels.label = QStringLiteral("R G B");
    channels.value = QStringLiteral("%1 %2 %3").arg(rgb.value(0)).arg(rgb.value(1)).arg(rgb.value(2));
    channels.hint = QStringLiteral("0–7");
    channels.monospace = true;
    const int base = cramBase();
    sdk::Field slot;
    slot.key = QStringLiteral("cram");
    slot.label = tr("CRAM slot");
    slot.value = base >= 0 ? QString::number(base + entry_) : QStringLiteral("—");
    slot.monospace = true;
    QStringList used;
    for (int round : rounds()) {
        used << QString::number(round + 1);
    }
    sdk::Field roundsField;
    roundsField.key = QStringLiteral("rounds");
    roundsField.label = tr("Rounds");
    roundsField.value = used.isEmpty() ? tr("title, ending") : used.join(QStringLiteral(", "));
    inspection.fields = {value, channels, slot, roundsField};
    inspection.note = tr("Mega Drive colors have three bits per channel. Entry 0 of a "
                         "line is transparent in tiles and sprites.");
    inspection.engineTag = QStringLiteral("flicky.semantics.v1");
    inspection.engine = {{tr("Palette"), id_},
                         {tr("CRAM word"), hex(word, 4)},
                         {tr("Saved"),
                          hex(session_->savedSemantics().colour(id_, entry_), 4)},
                         {tr("Source"), QStringLiteral("graphics/semantics.json")}};
    return inspection;
}

QString SemanticsPaletteModel::statusText() const {
    return tr("%1 · entry %2 · %3")
        .arg(id_)
        .arg(QString::number(entry_, 16).toUpper())
        .arg(hex(session_->semantics().colour(id_, entry_), 3));
}

// ---------------------------------------------------------------- text

TextModel::TextModel(FlickySession* session) : TableModel(session), session_(session) {
    connect(session_, &FlickySession::semanticsChanged, this, &sdk::EditorModel::contentChanged);
}

QString TextModel::idAt(int row) const {
    const QStringList ids = session_->semantics().textIds();
    return row >= 0 && row < ids.size() ? ids.at(row) : QString();
}

QVector<sdk::TableColumn> TextModel::columns() const {
    sdk::TableColumn name;
    name.key = QStringLiteral("name");
    name.label = tr("Name");
    name.width = 240;
    name.muted = true;
    sdk::TableColumn text;
    text.key = QStringLiteral("text");
    text.label = tr("Game text");
    text.monospace = true;
    text.editable = true;
    sdk::TableColumn length;
    length.key = QStringLiteral("length");
    length.label = tr("Length");
    length.width = 170;
    length.budget = true;
    return {name, text, length};
}

int TextModel::rowCount() const {
    return session_->semantics().textIds().size();
}

QString TextModel::cell(int row, int column) const {
    const QString id = idAt(row);
    if (column == 0) return id;
    if (column == 1) return session_->semantics().textValue(id);
    return {};
}

sdk::Budget TextModel::cellBudget(int row, int) const {
    const QString id = idAt(row);
    sdk::Budget budget;
    budget.used = session_->semantics().textValue(id).size();
    budget.capacity = session_->semantics().textCapacity(id);
    return budget;
}

int TextModel::maxLength(int row, int) const {
    return session_->semantics().textCapacity(idAt(row));
}

bool TextModel::isRowDirty(int row) const {
    const QString id = idAt(row);
    return session_->semantics().textValue(id) != session_->savedSemantics().textValue(id);
}

void TextModel::setCurrentRow(int row) {
    if (row != row_ && row >= 0 && row < rowCount()) {
        row_ = row;
        emit selectionChanged();
    }
}

bool TextModel::setText(int row, const QString& value, QString* error) {
    const QString id = idAt(row);
    if (id.isEmpty()) {
        return false;
    }
    try {
        session_->semantics().validateTextValue(id, value);
    } catch (const std::exception& e) {
        if (error) {
            *error = QStringLiteral("%1: %2").arg(id, QString::fromUtf8(e.what()));
        }
        return false;
    }
    return session_->editSemantics(tr("Edit text %1").arg(id),
                                   [id, value](FlickySemanticsDocument& s) { s.setTextValue(id, value); },
                                   error);
}

bool TextModel::setCell(int row, int column, const QString& value, QString* error) {
    if (column != 1) {
        return TableModel::setCell(row, column, value, error);
    }
    return setText(row, value, error);
}

bool TextModel::setField(const QString& key, const QVariant& value, QString* error) {
    if (key == QLatin1String("text")) {
        return setText(row_, value.toString(), error);
    }
    return TableModel::setField(key, value, error);
}

sdk::Budget TextModel::budget() const {
    int used = 0;
    int capacity = 0;
    for (const QString& id : session_->semantics().textIds()) {
        used += session_->semantics().textValue(id).size();
        capacity += session_->semantics().textCapacity(id);
    }
    sdk::Budget budget;
    budget.used = used;
    budget.capacity = capacity;
    budget.text = tr("text %1 / %2 chars").arg(used).arg(capacity);
    return budget;
}

sdk::Inspection TextModel::inspect() const {
    const QString id = idAt(row_);
    const QString value = session_->semantics().textValue(id);
    const int capacity = session_->semantics().textCapacity(id);
    sdk::Inspection inspection;
    inspection.kicker = tr("String");
    inspection.title = id;
    sdk::Field text;
    text.key = QStringLiteral("text");
    text.label = QStringLiteral("Text");
    text.type = sdk::Field::Type::Text;
    text.value = value;
    text.hint = tr("max %1").arg(capacity);
    text.monospace = true;
    sdk::Field length;
    length.key = QStringLiteral("length");
    length.label = tr("Length");
    length.value = QStringLiteral("%1 / %2").arg(value.size()).arg(capacity);
    length.hint = tr("fixed slot");
    length.monospace = true;
    inspection.fields = {text, length};
    inspection.preview = value;
    inspection.note = tr("Each string has a fixed slot in the ROM. Printable ASCII only; "
                         "quotes and backslashes are not allowed.");
    // The game's font has kana where ASCII has lowercase letters.
    const auto lowercase = [](QChar c) { return c >= QLatin1Char('a') && c <= QLatin1Char('z'); };
    if (std::any_of(value.begin(), value.end(), lowercase)) {
        inspection.note += tr(" Lowercase letters draw as kana in Flicky's font.");
    }
    inspection.engineTag = QStringLiteral("flicky.semantics.v1");
    inspection.engine = {{tr("Id"), id},
                         {tr("Capacity"), tr("%1 bytes").arg(capacity)},
                         {tr("Saved"), session_->savedSemantics().textValue(id)},
                         {tr("At open"), session_->semantics().baselineTextValue(id)}};
    return inspection;
}

QString TextModel::statusText() const {
    const QString id = idAt(row_);
    return QStringLiteral("%1 · %2 / %3")
        .arg(id)
        .arg(session_->semantics().textValue(id).size())
        .arg(session_->semantics().textCapacity(id));
}

}  // namespace rl::flicky
