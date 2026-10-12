#include "solomon/data/text.hpp"

#include "sdk/game_names.hpp"
#include "sdk/i18n.hpp"

#include <QJsonArray>
#include <QStringList>

#include <algorithm>

namespace rl::solomon {

namespace {

constexpr int kSpace = 0x24;
constexpr int kOpenQuote = 0x28;
constexpr int kCloseQuote = 0x3A;
constexpr int kDoubleAddressMessages = 4;
const int kPalettePrograms[] = {1, 5, 13};

QChar markOf(int tile) {
    switch (tile) {
    case kSpace: return QLatin1Char(' ');
    case 0x25: return QLatin1Char(',');
    case kOpenQuote: return QChar(0x201C);
    case 0x29: return QLatin1Char('.');
    case 0x2A: return QChar(0x00A9);
    case 0x2B: return QChar(0x2665);
    case kCloseQuote: return QChar(0x201D);
    case 0x3B: return QLatin1Char('\'');
    default: return QChar();
    }
}

int tileOf(QChar c) {
    if (c >= QLatin1Char('0') && c <= QLatin1Char('9')) return c.unicode() - '0';
    const QChar upper = c.toUpper();
    if (upper >= QLatin1Char('A') && upper <= QLatin1Char('Z')) return 0x0A + upper.unicode() - 'A';
    for (int tile : {kSpace, 0x25, kOpenQuote, 0x29, 0x2A, 0x2B, kCloseQuote, 0x3B}) {
        if (markOf(tile) == c) return tile;
    }
    return -1;
}

QString programLabel(int program) {
    switch (program) {
    case 0: return QStringLiteral("HUD");
    case 2: return rlTr("solomon", "Room clear · thanks");
    case 3: return rlTr("solomon", "Room clear · released");
    case 4: return rlTr("solomon", "Room clear · next room");
    case 6: return rlTr("solomon", "Game over");
    case 9: return rlTr("solomon", "Title · records");
    case 10: return rlTr("solomon", "Title · start");
    case 11: return rlTr("solomon", "Title · copyright");
    case 15: return rlTr("solomon", "Licence · title");
    case 16: return rlTr("solomon", "Licence · copyright");
    case 17: return rlTr("solomon", "Licence · licensed by");
    default: return rlTr("solomon", "Program %1").arg(program, 2, 10, QLatin1Char('0'));
    }
}

bool isText(const QJsonObject& command) {
    const int address = command.value(QStringLiteral("address")).toInt();
    return command.value(QStringLiteral("literal")).toBool() && address < 0x3F00 && (address & 0x3ff) < 0x3c0;
}

QVector<int> ints(const QJsonValue& value) {
    QVector<int> result;
    for (const QJsonValue& v : value.toArray()) result << v.toInt();
    return result;
}

QJsonArray array(const QVector<int>& values) {
    QJsonArray result;
    for (int v : values) result.append(v);
    return result;
}

int programBytes(const QJsonObject& program) {
    int bytes = 1;
    for (const QJsonValue& value : program.value(QStringLiteral("commands")).toArray()) {
        const QJsonObject c = value.toObject();
        bytes += 3 + (c.value(QStringLiteral("literal")).toBool() ? c.value(QStringLiteral("bytes")).toArray().size() : 1);
    }
    return bytes;
}

int messageBytes(const QJsonObject& message) {
    return 1 + message.value(QStringLiteral("tiles")).toArray().size() + 1 +
           2 * message.value(QStringLiteral("addresses")).toArray().size();
}

}  // namespace

QString tilesToText(const QVector<int>& tiles) {
    QString text;
    for (int tile : tiles) {
        if (tile >= 0 && tile <= 9) text += QChar('0' + tile);
        else if (tile >= 0x0A && tile <= 0x23) text += QChar('A' + tile - 0x0A);
        else if (!markOf(tile).isNull()) text += markOf(tile);
        else text += QStringLiteral("{%1}").arg(tile, 2, 16, QLatin1Char('0')).toUpper();
    }
    return text;
}

QVector<int> textToTiles(const QString& text, QString* error) {
    QVector<int> tiles;
    for (int i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (c == QLatin1Char('{')) {
            const int close = text.indexOf(QLatin1Char('}'), i);
            bool ok = false;
            const int value = close > i ? text.mid(i + 1, close - i - 1).toInt(&ok, 16) : -1;
            if (!ok || value < 0 || value > 255) {
                if (error) *error = rlTr("solomon", "Write a tile as {XX} with two hex digits.");
                return {};
            }
            tiles << value;
            i = close;
            continue;
        }
        if (c == QLatin1Char('"')) {
            // Straight quotes open after a space or at the start, else close.
            tiles << (tiles.isEmpty() || tiles.last() == kSpace ? kOpenQuote : kCloseQuote);
            continue;
        }
        const int tile = tileOf(c);
        if (tile < 0) {
            if (error) {
                *error = rlTr("solomon", "The font has no \"%1\". It has A-Z, 0-9, space and , . ' \" © ♥; "
                                         "write other tiles as {XX}.").arg(c);
            }
            return {};
        }
        tiles << tile;
    }
    return tiles;
}

QVector<TextString> SolomonText::strings() const {
    QVector<TextString> result;
    const QJsonArray programs = root_.value(QStringLiteral("static_programs")).toArray();
    for (int p = 0; p < programs.size(); ++p) {
        const QJsonArray commands = programs.at(p).toObject().value(QStringLiteral("commands")).toArray();
        int lines = 0;
        for (const QJsonValue& value : commands) lines += isText(value.toObject());
        int line = 0;
        for (int c = 0; c < commands.size(); ++c) {
            const QJsonObject command = commands.at(c).toObject();
            if (!isText(command)) continue;
            TextString s;
            s.id = QStringLiteral("s:%1:%2").arg(p).arg(c);
            s.label = lines > 1 ? rlTr("solomon", "%1 · line %2").arg(programLabel(p)).arg(++line) : programLabel(p);
            s.tiles = ints(command.value(QStringLiteral("bytes")));
            s.addresses = {command.value(QStringLiteral("address")).toInt()};
            s.vertical = command.value(QStringLiteral("vertical")).toBool();
            s.movable = true;
            result << s;
        }
    }
    const QJsonArray intro = root_.value(QStringLiteral("room_intro")).toArray();
    static const char* const kIntro[] = {"Room intro · place", "Room intro · room"};
    for (int r = 0; r < 2 && r < intro.size(); ++r) {
        const QJsonObject command = intro.at(r).toObject();
        TextString s;
        s.id = QStringLiteral("r:%1").arg(r);
        s.label = QString::fromUtf8(kIntro[r]);
        s.tiles = ints(command.value(QStringLiteral("bytes")));
        s.addresses = {command.value(QStringLiteral("address")).toInt()};
        s.width = s.tiles.size();
        result << s;
    }
    const QJsonArray names = root_.value(QStringLiteral("special_room_names")).toArray();
    for (int n = 0; n < names.size() && n < 3; ++n) {
        TextString s;
        s.id = QStringLiteral("n:%1").arg(n);
        // The three special rooms' names, as the game calls the rooms.
        s.label = rlTr("solomon", "Room intro · %1")
                      .arg(sdk::gameName("places", n, rlTr("solomon", "special room %1").arg(n + 1)));
        s.tiles = ints(names.at(n));
        // The name replaces the room line from its first cell.
        s.addresses = {intro.at(1).toObject().value(QStringLiteral("address")).toInt()};
        s.width = s.tiles.size();
        result << s;
    }
    const QJsonArray initial = root_.value(QStringLiteral("ending_initial")).toArray();
    for (int i = 0; i < initial.size(); ++i) {
        TextString s;
        s.id = QStringLiteral("i:%1").arg(i);
        s.label = rlTr("solomon", "Ending · fairies, line %1").arg(i + 1);
        s.tiles = ints(initial.at(i).toObject().value(QStringLiteral("tiles")));
        s.addresses = ints(initial.at(i).toObject().value(QStringLiteral("addresses")));
        s.movable = true;
        result << s;
    }
    const QJsonArray messages = root_.value(QStringLiteral("ending_messages")).toArray();
    for (int m = 0; m < messages.size(); ++m) {
        TextString s;
        s.id = QStringLiteral("m:%1").arg(m);
        s.label = rlTr("solomon", "Ending · message %1").arg(m + 1);
        s.tiles = ints(messages.at(m).toObject().value(QStringLiteral("tiles")));
        s.addresses = ints(messages.at(m).toObject().value(QStringLiteral("addresses")));
        s.movable = true;
        result << s;
    }
    return result;
}

TextString SolomonText::string(const QString& id) const {
    for (const TextString& s : strings()) {
        if (s.id == id) return s;
    }
    return {};
}

void SolomonText::setTiles(const QString& id, const QVector<int>& tiles) {
    const QStringList parts = id.split(QLatin1Char(':'));
    const QString kind = parts.value(0);
    const int a = parts.value(1).toInt();
    if (kind == QLatin1String("s")) {
        QJsonArray programs = root_.value(QStringLiteral("static_programs")).toArray();
        QJsonObject program = programs.at(a).toObject();
        QJsonArray commands = program.value(QStringLiteral("commands")).toArray();
        const int c = parts.value(2).toInt();
        QJsonObject command = commands.at(c).toObject();
        command.insert(QStringLiteral("bytes"), array(tiles));
        commands.replace(c, command);
        program.insert(QStringLiteral("commands"), commands);
        programs.replace(a, program);
        root_.insert(QStringLiteral("static_programs"), programs);
    } else if (kind == QLatin1String("r")) {
        QJsonArray intro = root_.value(QStringLiteral("room_intro")).toArray();
        QJsonObject command = intro.at(a).toObject();
        command.insert(QStringLiteral("bytes"), array(tiles));
        intro.replace(a, command);
        root_.insert(QStringLiteral("room_intro"), intro);
    } else if (kind == QLatin1String("n")) {
        QJsonArray names = root_.value(QStringLiteral("special_room_names")).toArray();
        names.replace(a, array(tiles));
        root_.insert(QStringLiteral("special_room_names"), names);
    } else if (kind == QLatin1String("i") || kind == QLatin1String("m")) {
        const QString key = kind == QLatin1String("i") ? QStringLiteral("ending_initial") : QStringLiteral("ending_messages");
        QJsonArray list = root_.value(key).toArray();
        QJsonObject message = list.at(a).toObject();
        message.insert(QStringLiteral("tiles"), array(tiles));
        list.replace(a, message);
        root_.insert(key, list);
    }
}

void SolomonText::move(const QString& id, int delta) {
    const QStringList parts = id.split(QLatin1Char(':'));
    const QString kind = parts.value(0);
    const int a = parts.value(1).toInt();
    if (kind == QLatin1String("s")) {
        QJsonArray programs = root_.value(QStringLiteral("static_programs")).toArray();
        QJsonObject program = programs.at(a).toObject();
        QJsonArray commands = program.value(QStringLiteral("commands")).toArray();
        const int c = parts.value(2).toInt();
        QJsonObject command = commands.at(c).toObject();
        command.insert(QStringLiteral("address"), command.value(QStringLiteral("address")).toInt() + delta);
        commands.replace(c, command);
        program.insert(QStringLiteral("commands"), commands);
        programs.replace(a, program);
        root_.insert(QStringLiteral("static_programs"), programs);
    } else if (kind == QLatin1String("i") || kind == QLatin1String("m")) {
        const QString key = kind == QLatin1String("i") ? QStringLiteral("ending_initial") : QStringLiteral("ending_messages");
        QJsonArray list = root_.value(key).toArray();
        QJsonObject message = list.at(a).toObject();
        QJsonArray addresses;
        for (int address : ints(message.value(QStringLiteral("addresses")))) addresses.append(address + delta);
        message.insert(QStringLiteral("addresses"), addresses);
        list.replace(a, message);
        root_.insert(key, list);
    }
}

QVector<int> SolomonText::palette(int program) const {
    const QJsonArray commands = root_.value(QStringLiteral("static_programs")).toArray().at(program).toObject()
                                    .value(QStringLiteral("commands")).toArray();
    for (const QJsonValue& value : commands) {
        const QJsonObject c = value.toObject();
        if (c.value(QStringLiteral("address")).toInt() == 0x3F00) return ints(c.value(QStringLiteral("bytes")));
    }
    return {};
}

void SolomonText::setPaletteEntry(int program, int entry, int value) {
    if (std::find(std::begin(kPalettePrograms), std::end(kPalettePrograms), program) == std::end(kPalettePrograms)) return;
    QJsonArray programs = root_.value(QStringLiteral("static_programs")).toArray();
    QJsonObject p = programs.at(program).toObject();
    QJsonArray commands = p.value(QStringLiteral("commands")).toArray();
    for (int c = 0; c < commands.size(); ++c) {
        QJsonObject command = commands.at(c).toObject();
        if (command.value(QStringLiteral("address")).toInt() != 0x3F00) continue;
        QJsonArray bytes = command.value(QStringLiteral("bytes")).toArray();
        if (entry < 0 || entry >= bytes.size()) return;
        bytes.replace(entry, value);
        command.insert(QStringLiteral("bytes"), bytes);
        commands.replace(c, command);
    }
    p.insert(QStringLiteral("commands"), commands);
    programs.replace(program, p);
    root_.insert(QStringLiteral("static_programs"), programs);
}

SolomonText::Usage SolomonText::staticUsage() const {
    Usage usage;
    for (const QJsonValue& value : root_.value(QStringLiteral("static_programs")).toArray()) {
        usage.used += programBytes(value.toObject());
    }
    usage.capacity = root_.value(QStringLiteral("layout")).toObject().value(QStringLiteral("static_bytes")).toInt();
    return usage;
}

SolomonText::Usage SolomonText::initialUsage() const {
    Usage usage;
    for (const QJsonValue& value : root_.value(QStringLiteral("ending_initial")).toArray()) {
        usage.used += messageBytes(value.toObject());
    }
    usage.capacity = root_.value(QStringLiteral("layout")).toObject().value(QStringLiteral("ending_initial_bytes")).toInt();
    return usage;
}

SolomonText::Usage SolomonText::messageUsage() const {
    Usage usage;
    for (const QJsonValue& value : root_.value(QStringLiteral("ending_messages")).toArray()) {
        usage.used += messageBytes(value.toObject());
    }
    usage.capacity = root_.value(QStringLiteral("layout")).toObject().value(QStringLiteral("ending_message_bytes")).toInt();
    return usage;
}

QString SolomonText::overflow() const {
    const Usage s = staticUsage();
    if (s.used > s.capacity) {
        return rlTr("solomon", "The HUD, room-clear, title and licence text share %1 bytes; they need %2. Shorten "
                               "another line first.").arg(s.capacity).arg(s.used);
    }
    const Usage i = initialUsage();
    if (i.used > i.capacity) {
        return rlTr("solomon", "The two fairy lines of the ending share %1 bytes; they need %2.").arg(i.capacity).arg(i.used);
    }
    const Usage m = messageUsage();
    if (m.used > m.capacity) {
        return rlTr("solomon", "The ending messages share %1 bytes; they need %2. Shorten another message first.")
            .arg(m.capacity).arg(m.used);
    }
    for (const TextString& str : strings()) {
        if (str.tiles.isEmpty() || str.tiles.size() > 64) {
            return rlTr("solomon", "%1 needs 1-64 characters.").arg(str.label);
        }
        if ((str.id.startsWith(QLatin1Char('i')) || str.id.startsWith(QLatin1Char('m'))) && str.tiles.contains(0)) {
            return rlTr("solomon", "%1 cannot use 0: the digit ends an ending message. Write O instead.").arg(str.label);
        }
    }
    return {};
}

}  // namespace rl::solomon
