#include "smb/data/screens.hpp"

#include "smb/data/resource_document.hpp"

#include <QHash>
#include <QJsonValue>

#include <algorithm>

namespace rl::smb {

namespace {

[[noreturn]] void fail(const QString& message) {
    throw SmbError(message.toStdString());
}

QVector<int> intsOf(const QJsonValue& value) {
    QVector<int> result;
    for (const QJsonValue& item : value.toArray()) {
        result << item.toInt();
    }
    return result;
}

QJsonArray arrayOf(const QVector<int>& values) {
    QJsonArray result;
    for (int value : values) {
        result.append(value);
    }
    return result;
}

}  // namespace

// ---------------------------------------------------------------- encoding

SmbTextEncoding SmbTextEncoding::fromJson(const QJsonObject& json) {
    SmbTextEncoding encoding;
    encoding.characters = json.value(QStringLiteral("characters")).toString();
    encoding.codes = intsOf(json.value(QStringLiteral("codes")));
    encoding.caseFold = json.value(QStringLiteral("case_fold")).toString(QStringLiteral("none"));
    encoding.padding = json.value(QStringLiteral("padding")).toInt();
    if (encoding.codes.size() != encoding.characters.size()) {
        encoding.characters.clear();
        encoding.codes.clear();
    }
    return encoding;
}

QString SmbTextEncoding::fold(const QString& text) const {
    if (caseFold == QLatin1String("upper")) return text.toUpper();
    if (caseFold == QLatin1String("lower")) return text.toLower();
    return text;
}

int SmbTextEncoding::code(QChar c) const {
    const int index = characters.indexOf(c);
    return index < 0 ? -1 : codes.at(index);
}

QChar SmbTextEncoding::character(int value) const {
    const int index = codes.indexOf(value);
    return index < 0 ? QChar() : characters.at(index);
}

QString SmbTextEncoding::decode(const QVector<int>& values) const {
    QString text;
    for (int value : values) {
        const QChar c = character(value);
        text += c.isNull() ? QLatin1Char('?') : c;
    }
    const QChar pad = character(padding);
    while (!pad.isNull() && text.endsWith(pad)) {
        text.chop(1);
    }
    return text;
}

std::optional<QVector<int>> SmbTextEncoding::encode(const QString& text, int width, QString* error) const {
    const QString folded = fold(text);
    if (folded.size() > width) {
        if (error) {
            *error = QStringLiteral("The text has %1 characters; its slot holds %2.").arg(folded.size()).arg(width);
        }
        return std::nullopt;
    }
    QVector<int> result;
    QString missing;
    for (const QChar c : folded) {
        const int value = code(c);
        if (value < 0) {
            if (!missing.contains(c)) missing += c;
            continue;
        }
        result << value;
    }
    if (!missing.isEmpty()) {
        if (error) {
            *error = QStringLiteral("The game's font has no \"%1\". It has %2.").arg(missing, summary());
        }
        return std::nullopt;
    }
    while (result.size() < width) {
        result << padding;
    }
    return result;
}

QString SmbTextEncoding::summary() const {
    // Runs of digits and letters collapse to ranges: "0-9 A-Z space - !".
    QStringList parts;
    int i = 0;
    while (i < characters.size()) {
        int j = i;
        while (j + 1 < characters.size() && characters.at(j + 1).unicode() == characters.at(j).unicode() + 1 &&
               characters.at(j).isLetterOrNumber()) {
            ++j;
        }
        if (j - i >= 2) {
            parts << QStringLiteral("%1-%2").arg(characters.at(i)).arg(characters.at(j));
        } else {
            for (int k = i; k <= j; ++k) {
                parts << (characters.at(k) == QLatin1Char(' ') ? QStringLiteral("space") : QString(characters.at(k)));
            }
        }
        i = j + 1;
    }
    return parts.join(QLatin1Char(' '));
}

// ---------------------------------------------------------------- screen

QPoint SmbScreen::tileWriter(int tile) const {
    const int address = base + tile;
    QPoint writer(-1, -1);
    for (int c = 0; c < commands.size(); ++c) {
        const SmbScreenCommand& command = commands.at(c);
        for (int i = 0; i < command.length; ++i) {
            if (command.target(i) == address) {
                writer = QPoint(c, command.valueIndex(i));
            }
        }
    }
    return writer;
}

QPoint SmbScreen::attributeWriter(int index) const {
    const int address = attributeBase() + index;
    QPoint writer(-1, -1);
    for (int c = 0; c < commands.size(); ++c) {
        const SmbScreenCommand& command = commands.at(c);
        for (int i = 0; i < command.length; ++i) {
            if (command.target(i) == address) {
                writer = QPoint(c, command.valueIndex(i));
            }
        }
    }
    return writer;
}

QVector<int> SmbScreen::runOf(int tile) const {
    const QPoint writer = tileWriter(tile);
    if (writer.x() < 0 || !commands.at(writer.x()).repeat) {
        return {tile};
    }
    QVector<int> run;
    const SmbScreenCommand& command = commands.at(writer.x());
    for (int i = 0; i < command.length; ++i) {
        const int t = command.target(i) - base;
        if (t >= 0 && t < width * height && tileWriter(t) == writer) {
            run << t;
        }
    }
    return run;
}

int SmbScreen::attributeIndexOf(int tile) const {
    const int x = tile % width;
    const int y = tile / width;
    return (y / 4) * 8 + x / 4;
}

int SmbScreen::attributeShiftOf(int tile) const {
    const int x = tile % width;
    const int y = tile / width;
    return ((y / 2) % 2) * 4 + ((x / 2) % 2) * 2;
}

int SmbScreen::paletteOf(int tile) const {
    const int index = attributeIndexOf(tile);
    return index < attributes.size() ? (attributes.at(index) >> attributeShiftOf(tile)) & 3 : 0;
}

int SmbScreen::fillTile() const {
    QHash<int, int> counts;
    for (int tile = 0; tile < tiles.size(); ++tile) {
        if (tileWriter(tile).x() < 0) ++counts[tiles.at(tile)];
    }
    int best = 0x24;
    int bestCount = 0;
    for (auto it = counts.constBegin(); it != counts.constEnd(); ++it) {
        if (it.value() > bestCount) {
            best = it.key();
            bestCount = it.value();
        }
    }
    return best;
}

int SmbScreen::listBytes() const {
    int bytes = 1;
    for (const SmbScreenCommand& command : commands) {
        bytes += command.bytes();
    }
    return bytes;
}

QVector<int> SmbScreen::cellsOf(int c) const {
    QVector<int> cells;
    if (c < 0 || c >= commands.size()) return cells;
    const SmbScreenCommand& command = commands.at(c);
    for (int i = 0; i < command.length; ++i) {
        const int cell = command.target(i) - base;
        if (cell < 0 || cell >= width * height) return {};
        cells << cell;
    }
    return cells;
}

// ---------------------------------------------------------------- set

SmbScreenSet SmbScreenSet::fromJson(const QJsonObject& resource) {
    SmbScreenSet set;
    for (const QJsonValue& value : resource.value(QStringLiteral("screens")).toArray()) {
        const QJsonObject json = value.toObject();
        SmbScreen screen;
        screen.key = json.value(QStringLiteral("key")).toString();
        screen.label = json.value(QStringLiteral("label")).toString();
        screen.capacity = json.value(QStringLiteral("capacity")).toInt();
        screen.width = json.value(QStringLiteral("width")).toInt(32);
        screen.height = json.value(QStringLiteral("height")).toInt(30);
        for (const QJsonValue& row : json.value(QStringLiteral("tiles")).toArray()) {
            screen.tiles += intsOf(row);
        }
        screen.attributes = intsOf(json.value(QStringLiteral("attributes")));
        for (const QJsonValue& item : json.value(QStringLiteral("commands")).toArray()) {
            const QJsonObject c = item.toObject();
            SmbScreenCommand command;
            command.address = c.value(QStringLiteral("address")).toInt();
            command.vertical = c.value(QStringLiteral("vertical")).toBool();
            command.repeat = c.value(QStringLiteral("repeat")).toBool();
            command.length = c.value(QStringLiteral("length")).toInt();
            command.values = intsOf(c.value(QStringLiteral("values")));
            screen.commands << command;
        }
        screen.trailing = json.value(QStringLiteral("trailing")).toArray();
        // The kit lays a screen on one name table; its commands say which.
        if (!screen.commands.isEmpty()) {
            screen.base = screen.commands.first().address & 0xfc00;
        }
        if (screen.tiles.size() != screen.width * screen.height) {
            fail(QStringLiteral("Screen %1 has %2 tiles, not %3.")
                     .arg(screen.key)
                     .arg(screen.tiles.size())
                     .arg(screen.width * screen.height));
        }
        set.screens << screen;
    }
    if (resource.contains(QStringLiteral("encoding"))) {
        set.encoding = SmbTextEncoding::fromJson(resource.value(QStringLiteral("encoding")).toObject());
    }
    for (const QJsonValue& value : resource.value(QStringLiteral("text_fields")).toArray()) {
        const QJsonObject json = value.toObject();
        SmbScreenTextField field;
        field.key = json.value(QStringLiteral("key")).toString();
        field.label = json.value(QStringLiteral("label")).toString();
        field.screen = json.value(QStringLiteral("screen")).toString();
        field.start = json.value(QStringLiteral("start")).toInt();
        field.width = json.value(QStringLiteral("width")).toInt();
        set.textFields << field;
    }
    set.pooled = resource.value(QStringLiteral("capacity_pool")).toBool();
    for (const QJsonValue& value : resource.value(QStringLiteral("growable_fields")).toArray()) {
        set.growable << value.toString();
    }
    return set;
}

QJsonObject SmbScreenSet::toJson(QJsonObject resource) const {
    QJsonArray screensJson = resource.value(QStringLiteral("screens")).toArray();
    for (int s = 0; s < screens.size() && s < screensJson.size(); ++s) {
        const SmbScreen& screen = screens.at(s);
        QJsonObject json = screensJson.at(s).toObject();
        QJsonArray rows;
        for (int y = 0; y < screen.height; ++y) {
            rows.append(arrayOf(screen.tiles.mid(y * screen.width, screen.width)));
        }
        json.insert(QStringLiteral("tiles"), rows);
        json.insert(QStringLiteral("attributes"), arrayOf(screen.attributes));
        QJsonArray commands;
        for (const SmbScreenCommand& command : screen.commands) {
            QJsonObject c;
            c.insert(QStringLiteral("address"), command.address);
            c.insert(QStringLiteral("vertical"), command.vertical);
            c.insert(QStringLiteral("repeat"), command.repeat);
            c.insert(QStringLiteral("length"), command.length);
            c.insert(QStringLiteral("values"), arrayOf(command.values));
            commands.append(c);
        }
        json.insert(QStringLiteral("commands"), commands);
        screensJson.replace(s, json);
    }
    resource.insert(QStringLiteral("screens"), screensJson);
    QJsonArray fieldsJson = resource.value(QStringLiteral("text_fields")).toArray();
    for (int i = 0; i < fieldsJson.size(); ++i) {
        QJsonObject json = fieldsJson.at(i).toObject();
        for (const SmbScreenTextField& field : textFields) {
            if (field.key == json.value(QStringLiteral("key")).toString()) {
                json.insert(QStringLiteral("width"), field.width);
            }
        }
        fieldsJson.replace(i, json);
    }
    if (!fieldsJson.isEmpty()) {
        resource.insert(QStringLiteral("text_fields"), fieldsJson);
    }
    return resource;
}

SmbScreenPool SmbScreenSet::pool() const {
    SmbScreenPool result;
    result.pooled = pooled;
    if (!pooled) {
        return result;
    }
    // As the kit packs them; trailing bytes stay only while everything fits.
    int trailing = 0;
    for (const SmbScreen& screen : screens) {
        result.size += screen.capacity;
        result.used += screen.listBytes();
        trailing += screen.trailing.size();
    }
    if (result.used + trailing <= result.size) {
        result.used += trailing;
    }
    return result;
}

int SmbScreenSet::writerOf(const SmbScreenTextField& field) const {
    const int s = indexOf(field.screen);
    if (s < 0) {
        return -1;
    }
    const SmbScreen& screen = screens.at(s);
    for (int c = 0; c < screen.commands.size(); ++c) {
        const SmbScreenCommand& command = screen.commands.at(c);
        if (command.address == screen.base + field.start && !command.vertical && !command.repeat &&
            command.length == field.width) {
            return c;
        }
    }
    return -1;
}

bool SmbScreenSet::canGrow(const SmbScreenTextField& field) const {
    return pooled && growable.contains(field.key) && writerOf(field) >= 0;
}

int SmbScreenSet::maxWidth(const SmbScreenTextField& field) const {
    if (!canGrow(field)) {
        return field.width;
    }
    const int s = indexOf(field.screen);
    const int rowWidth = s >= 0 ? screens.at(s).width : 32;
    return std::min(63, rowWidth - field.start % rowWidth);
}

int SmbScreenSet::indexOf(const QString& key) const {
    for (int i = 0; i < screens.size(); ++i) {
        if (screens.at(i).key == key) {
            return i;
        }
    }
    return -1;
}

void SmbScreenSet::project(SmbScreen& screen) const {
    // Tiles nothing writes keep the fill they have; the commands, in order,
    // write the rest.
    const int tileCount = screen.width * screen.height;
    for (const SmbScreenCommand& command : screen.commands) {
        for (int i = 0; i < command.length; ++i) {
            const int target = command.target(i);
            const int value = command.values.value(command.valueIndex(i));
            if (target >= screen.base && target < screen.base + tileCount) {
                screen.tiles[target - screen.base] = value;
            } else if (target >= screen.attributeBase() && target < screen.attributeBase() + screen.attributes.size()) {
                screen.attributes[target - screen.attributeBase()] = value;
            }
        }
    }
}

void SmbScreenSet::setTile(int s, int tile, int value) {
    if (s < 0 || s >= screens.size()) fail(QStringLiteral("No such screen."));
    SmbScreen& screen = screens[s];
    if (tile < 0 || tile >= screen.tiles.size()) fail(QStringLiteral("The tile is outside the screen."));
    if (value < 0 || value > 255) fail(QStringLiteral("A tile is 0-255."));
    const QPoint writer = screen.tileWriter(tile);
    if (writer.x() < 0) {
        fail(QStringLiteral("The game never writes this cell of %1; it stays the fill tile.").arg(screen.label));
    }
    screen.commands[writer.x()].values[writer.y()] = value;
    project(screen);
}

void SmbScreenSet::setAttribute(int s, int index, int value) {
    if (s < 0 || s >= screens.size()) fail(QStringLiteral("No such screen."));
    SmbScreen& screen = screens[s];
    if (index < 0 || index >= screen.attributes.size()) fail(QStringLiteral("No such attribute byte."));
    const QPoint writer = screen.attributeWriter(index);
    if (writer.x() < 0) {
        fail(QStringLiteral("%1 does not write the palettes of this block.").arg(screen.label));
    }
    screen.commands[writer.x()].values[writer.y()] = value & 0xff;
    project(screen);
}

void SmbScreenSet::setPalette(int s, int tile, int palette) {
    if (s < 0 || s >= screens.size()) fail(QStringLiteral("No such screen."));
    const SmbScreen& screen = screens.at(s);
    const int index = screen.attributeIndexOf(tile);
    const int shift = screen.attributeShiftOf(tile);
    const int current = screen.attributes.value(index);
    setAttribute(s, index, (current & ~(3 << shift)) | ((palette & 3) << shift));
}

void SmbScreenSet::moveCommand(int s, int c, int cell, bool vertical) {
    if (s < 0 || s >= screens.size()) fail(QStringLiteral("No such screen."));
    SmbScreen& screen = screens[s];
    if (c < 0 || c >= screen.commands.size() || screen.cellsOf(c).isEmpty()) {
        fail(QStringLiteral("Only writes to the name table can move."));
    }
    const SmbScreenCommand& command = screen.commands.at(c);
    const int x = cell % screen.width;
    const int y = cell / screen.width;
    if (cell < 0 || y >= screen.height ||
        (vertical ? y + command.length > screen.height : x + command.length > screen.width)) {
        fail(QStringLiteral("The write would run off the screen; it keeps its %1 cells %2.")
                 .arg(command.length)
                 .arg(vertical ? QStringLiteral("down") : QStringLiteral("across")));
    }
    const int fill = screen.fillTile();
    screen.commands[c].address = screen.base + cell;
    screen.commands[c].vertical = vertical;
    // The name table again from the fill, as the kit projects it.
    for (int& tile : screen.tiles) tile = fill;
    project(screen);
}

QString SmbScreenSet::text(const SmbScreenTextField& field) const {
    const int s = indexOf(field.screen);
    if (s < 0) {
        return {};
    }
    return encoding.decode(screens.at(s).tiles.mid(field.start, field.width));
}

void SmbScreenSet::resizeText(const SmbScreenTextField& field, const QString& text) {
    const int s = indexOf(field.screen);
    const int c = writerOf(field);
    const int width = std::max(1, int(encoding.fold(text).size()));
    if (width > maxWidth(field)) {
        fail(QStringLiteral("The text has %1 characters; %2 holds %3 to the end of its row.")
                 .arg(width)
                 .arg(field.label)
                 .arg(maxWidth(field)));
    }
    for (const SmbScreenTextField& other : textFields) {
        const int row = screens.at(s).width;
        if (other.key != field.key && other.screen == field.screen && other.start / row == field.start / row &&
            other.start >= field.start && other.start < field.start + width) {
            fail(QStringLiteral("%1 would run into %2.").arg(field.label, other.label));
        }
    }
    QString error;
    const std::optional<QVector<int>> codes = encoding.encode(text, width, &error);
    if (!codes) fail(error);
    // On a copy, so a text that does not fit leaves the set as it was.
    SmbScreenSet next = *this;
    SmbScreen& screen = next.screens[s];
    const int fill = screen.fillTile();
    screen.commands[c].length = width;
    screen.commands[c].values = *codes;
    // The name table again from the fill, as the kit projects it.
    for (int& tile : screen.tiles) tile = fill;
    project(screen);
    for (SmbScreenTextField& f : next.textFields) {
        if (f.key == field.key) {
            f.width = width;
        }
    }
    const SmbScreenPool shared = next.pool();
    if (shared.used > shared.size) {
        fail(QStringLiteral("The game screens share %1 bytes; this text needs %2. Shorten another screen "
                            "text first.")
                 .arg(shared.size)
                 .arg(shared.used));
    }
    *this = next;
}

void SmbScreenSet::setText(const SmbScreenTextField& field, const QString& text) {
    const int s = indexOf(field.screen);
    if (s < 0) fail(QStringLiteral("The text field %1 names no screen.").arg(field.key));
    if (canGrow(field)) {
        // The write is as long as the text: shorter text frees its bytes.
        resizeText(field, text);
        return;
    }
    QString error;
    const std::optional<QVector<int>> codes = encoding.encode(text, field.width, &error);
    if (!codes) fail(error);
    for (int i = 0; i < field.width; ++i) {
        const int tile = field.start + i;
        if (screens.at(s).runOf(tile).size() > 1 && screens.at(s).tiles.at(tile) != codes->at(i)) {
            fail(QStringLiteral("Character %1 of %2 sits in a repeated run of the display list; the game "
                                "writes it together with its neighbours.")
                     .arg(i + 1)
                     .arg(field.label));
        }
        if (screens.at(s).tiles.at(tile) != codes->at(i)) {
            setTile(s, tile, codes->at(i));
        }
    }
}

// ---------------------------------------------------------------- text slots

QVector<SmbTextSlot> textSlots(const SmbResourceDocument& document) {
    QVector<SmbTextSlot> result;
    for (const QString& id : document.resourceIds(QStringLiteral("nes.fixed-text-fields.v1"))) {
        for (const QJsonValue& value : document.resource(id).value(QStringLiteral("messages")).toArray()) {
            const QJsonObject message = value.toObject();
            SmbTextSlot slot;
            slot.resource = id;
            slot.key = message.value(QStringLiteral("key")).toString();
            slot.label = message.value(QStringLiteral("label")).toString();
            slot.width = message.value(QStringLiteral("width")).toInt();
            slot.maxWidth = slot.width;
            result << slot;
        }
    }
    for (const QString& id : document.resourceIds(QStringLiteral("nes.ppu-screen-packets.v1"))) {
        const SmbScreenSet set = SmbScreenSet::fromJson(document.resource(id));
        for (const SmbScreenTextField& field : set.textFields) {
            SmbTextSlot slot;
            slot.resource = id;
            slot.key = field.key;
            slot.label = field.label;
            slot.screen = field.screen;
            const int s = set.indexOf(field.screen);
            slot.screenLabel = s >= 0 ? set.screens.at(s).label : field.screen;
            slot.width = field.width;
            slot.maxWidth = set.maxWidth(field);
            result << slot;
        }
    }
    return result;
}

SmbScreenPool screenPool(const SmbResourceDocument& document) {
    SmbScreenPool result;
    for (const QString& id : document.resourceIds(QStringLiteral("nes.ppu-screen-packets.v1"))) {
        const SmbScreenPool pool = SmbScreenSet::fromJson(document.resource(id)).pool();
        if (pool.pooled) {
            result.pooled = true;
            result.used += pool.used;
            result.size += pool.size;
        }
    }
    return result;
}

SmbTextEncoding slotEncoding(const SmbResourceDocument& document, const SmbTextSlot& slot) {
    return SmbTextEncoding::fromJson(document.resource(slot.resource).value(QStringLiteral("encoding")).toObject());
}

QString slotText(const SmbResourceDocument& document, const SmbTextSlot& slot) {
    const QJsonObject resource = document.resource(slot.resource);
    if (slot.screen.isEmpty()) {
        for (const QJsonValue& value : resource.value(QStringLiteral("messages")).toArray()) {
            if (value.toObject().value(QStringLiteral("key")).toString() == slot.key) {
                return value.toObject().value(QStringLiteral("text")).toString();
            }
        }
        return {};
    }
    const SmbScreenSet set = SmbScreenSet::fromJson(resource);
    for (const SmbScreenTextField& field : set.textFields) {
        if (field.key == slot.key) {
            return set.text(field);
        }
    }
    return {};
}

void setSlotText(SmbResourceDocument& document, const SmbTextSlot& slot, const QString& text) {
    QJsonObject resource = document.resource(slot.resource);
    if (slot.screen.isEmpty()) {
        const SmbTextEncoding encoding = slotEncoding(document, slot);
        QString error;
        if (!encoding.encode(text, slot.width, &error)) {
            fail(error);
        }
        QJsonArray messages = resource.value(QStringLiteral("messages")).toArray();
        for (int i = 0; i < messages.size(); ++i) {
            QJsonObject message = messages.at(i).toObject();
            if (message.value(QStringLiteral("key")).toString() == slot.key) {
                message.insert(QStringLiteral("text"), encoding.fold(text));
                messages.replace(i, message);
            }
        }
        resource.insert(QStringLiteral("messages"), messages);
        document.setResource(slot.resource, resource);
        return;
    }
    SmbScreenSet set = SmbScreenSet::fromJson(resource);
    for (const SmbScreenTextField& field : set.textFields) {
        if (field.key == slot.key) {
            // A copy: a text that grows replaces the set, fields and all.
            const SmbScreenTextField target = field;
            set.setText(target, text);
            break;
        }
    }
    document.setResource(slot.resource, set.toJson(resource));
}

}  // namespace rl::smb
