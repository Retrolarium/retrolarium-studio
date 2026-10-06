#include "sdk/project_file.hpp"

#include "sdk/i18n.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <QMap>

#include <memory>
#include <stdexcept>
#include <vector>

namespace rl::sdk {

namespace {

struct ParseError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

class Reader {
public:
    explicit Reader(const QString& text) : text_(text) {}

    QVariantMap parse() {
        QVariantMap root;
        QStringList table;       // current table path
        bool arrayTable = false; // current table is the last element of an array
        while (true) {
            skipBlankLinesAndComments();
            if (atEnd()) {
                break;
            }
            if (peek() == QLatin1Char('[')) {
                arrayTable = peekAt(1) == QLatin1Char('[');
                pos_ += arrayTable ? 2 : 1;
                table = readKeyPath();
                expect(QLatin1Char(']'));
                if (arrayTable) {
                    expect(QLatin1Char(']'));
                    appendTable(root, table);
                } else {
                    ensureTable(root, table);
                }
                endOfLine();
                continue;
            }
            const QStringList key = readKeyPath();
            skipSpaces();
            expect(QLatin1Char('='));
            skipSpaces();
            const QVariant value = readValue();
            endOfLine();
            QStringList full = table;
            full.append(key);
            assign(root, full, value, arrayTable ? table.size() : -1);
        }
        return toVariant(*root_);
    }

private:
    bool atEnd() const { return pos_ >= text_.size(); }
    QChar peek() const { return atEnd() ? QChar() : text_.at(pos_); }
    QChar peekAt(int offset) const {
        return pos_ + offset < text_.size() ? text_.at(pos_ + offset) : QChar();
    }

    [[noreturn]] void fail(const QString& message) const {
        const int line = text_.leftRef(pos_).count(QLatin1Char('\n')) + 1;
        throw ParseError(rlTr("studio", "line %1: %2").arg(line).arg(message).toStdString());
    }

    void expect(QChar c) {
        skipSpaces();
        if (peek() != c) {
            fail(rlTr("studio", "expected '%1'").arg(c));
        }
        ++pos_;
    }

    void skipSpaces() {
        while (!atEnd() && (peek() == QLatin1Char(' ') || peek() == QLatin1Char('\t'))) {
            ++pos_;
        }
    }

    void skipComment() {
        if (peek() == QLatin1Char('#')) {
            while (!atEnd() && peek() != QLatin1Char('\n')) {
                ++pos_;
            }
        }
    }

    void skipBlankLinesAndComments() {
        while (!atEnd()) {
            skipSpaces();
            skipComment();
            if (peek() == QLatin1Char('\r') || peek() == QLatin1Char('\n')) {
                ++pos_;
                continue;
            }
            break;
        }
    }

    // Whitespace, comments and newlines inside arrays.
    void skipArrayFiller() {
        while (!atEnd()) {
            const QChar c = peek();
            if (c == QLatin1Char(' ') || c == QLatin1Char('\t') || c == QLatin1Char('\r') ||
                c == QLatin1Char('\n')) {
                ++pos_;
            } else if (c == QLatin1Char('#')) {
                skipComment();
            } else {
                break;
            }
        }
    }

    void endOfLine() {
        skipSpaces();
        skipComment();
        if (peek() == QLatin1Char('\r')) {
            ++pos_;
        }
        if (!atEnd() && peek() != QLatin1Char('\n')) {
            fail(rlTr("studio", "unexpected text after value"));
        }
        if (!atEnd()) {
            ++pos_;
        }
    }

    QString readKey() {
        skipSpaces();
        if (peek() == QLatin1Char('"')) {
            return readBasicString();
        }
        if (peek() == QLatin1Char('\'')) {
            return readLiteralString();
        }
        const int start = pos_;
        while (!atEnd()) {
            const QChar c = peek();
            if (c.isLetterOrNumber() || c == QLatin1Char('_') || c == QLatin1Char('-')) {
                ++pos_;
            } else {
                break;
            }
        }
        if (pos_ == start) {
            fail(rlTr("studio", "expected a key"));
        }
        return text_.mid(start, pos_ - start);
    }

    QStringList readKeyPath() {
        QStringList parts{readKey()};
        skipSpaces();
        while (peek() == QLatin1Char('.')) {
            ++pos_;
            parts.append(readKey());
            skipSpaces();
        }
        return parts;
    }

    QString readBasicString() {
        ++pos_;  // opening quote
        QString result;
        while (true) {
            if (atEnd() || peek() == QLatin1Char('\n')) {
                fail(rlTr("studio", "unterminated string"));
            }
            const QChar c = text_.at(pos_++);
            if (c == QLatin1Char('"')) {
                return result;
            }
            if (c != QLatin1Char('\\')) {
                result.append(c);
                continue;
            }
            if (atEnd()) {
                fail(rlTr("studio", "unterminated escape"));
            }
            const QChar e = text_.at(pos_++);
            switch (e.unicode()) {
            case 'n': result.append(QLatin1Char('\n')); break;
            case 't': result.append(QLatin1Char('\t')); break;
            case 'r': result.append(QLatin1Char('\r')); break;
            case 'b': result.append(QChar(8)); break;
            case 'f': result.append(QChar(12)); break;
            case '"': result.append(QLatin1Char('"')); break;
            case '\\': result.append(QLatin1Char('\\')); break;
            case 'u':
            case 'U': {
                const int digits = e == QLatin1Char('u') ? 4 : 8;
                bool ok = false;
                const uint code = text_.mid(pos_, digits).toUInt(&ok, 16);
                if (!ok) {
                    fail(rlTr("studio", "bad unicode escape"));
                }
                pos_ += digits;
                result.append(QString::fromUcs4(&code, 1));
                break;
            }
            default:
                fail(rlTr("studio", "unknown escape \\%1").arg(e));
            }
        }
    }

    QString readLiteralString() {
        ++pos_;
        const int start = pos_;
        while (!atEnd() && peek() != QLatin1Char('\'')) {
            if (peek() == QLatin1Char('\n')) {
                fail(rlTr("studio", "unterminated string"));
            }
            ++pos_;
        }
        if (atEnd()) {
            fail(rlTr("studio", "unterminated string"));
        }
        const QString result = text_.mid(start, pos_ - start);
        ++pos_;
        return result;
    }

    QVariant readValue() {
        skipSpaces();
        const QChar c = peek();
        if (c == QLatin1Char('"')) {
            if (peekAt(1) == QLatin1Char('"') && peekAt(2) == QLatin1Char('"')) {
                fail(rlTr("studio", "multi-line strings are not supported"));
            }
            return readBasicString();
        }
        if (c == QLatin1Char('\'')) {
            return readLiteralString();
        }
        if (c == QLatin1Char('[')) {
            // Bounded, so a crafted file cannot exhaust the stack.
            if (depth_ >= kMaxArrayDepth) {
                fail(rlTr("studio", "arrays are nested too deeply"));
            }
            ++depth_;
            ++pos_;
            QVariantList items;
            skipArrayFiller();
            while (peek() != QLatin1Char(']')) {
                items.append(readValue());
                skipArrayFiller();
                if (peek() == QLatin1Char(',')) {
                    ++pos_;
                    skipArrayFiller();
                } else if (peek() != QLatin1Char(']')) {
                    fail(rlTr("studio", "expected ',' or ']' in array"));
                }
            }
            ++pos_;
            --depth_;
            return items;
        }
        if (c == QLatin1Char('{')) {
            fail(rlTr("studio", "inline tables are not supported"));
        }
        const int start = pos_;
        while (!atEnd()) {
            const QChar d = peek();
            if (d == QLatin1Char(',') || d == QLatin1Char(']') || d == QLatin1Char('#') ||
                d == QLatin1Char('\n') || d == QLatin1Char('\r') || d == QLatin1Char(' ') ||
                d == QLatin1Char('\t')) {
                break;
            }
            ++pos_;
        }
        QString token = text_.mid(start, pos_ - start);
        if (token == QLatin1String("true")) {
            return true;
        }
        if (token == QLatin1String("false")) {
            return false;
        }
        token.remove(QLatin1Char('_'));
        bool ok = false;
        const qlonglong integer = token.startsWith(QLatin1String("0x"))
                                      ? token.mid(2).toLongLong(&ok, 16)
                                      : token.toLongLong(&ok, 10);
        if (ok) {
            return integer;
        }
        const double real = token.toDouble(&ok);
        if (ok) {
            return real;
        }
        fail(rlTr("studio", "unsupported value '%1'").arg(token));
    }

    // Tables are built as shared nodes and converted to QVariantMap at the
    // end; QVariant's copy-on-write makes editing nested maps in place awkward.
    struct Table;
    using TablePtr = std::shared_ptr<Table>;
    struct Entry {
        QVariant scalar;
        TablePtr table;
        std::vector<TablePtr> array;  // array of tables
        bool isArray = false;
    };
    struct Table {
        QMap<QString, Entry> entries;
    };

    // Walks `path`, creating tables; an array of tables resolves to its last
    // element, as TOML specifies for keys following [[name]].
    Table* descend(const QStringList& path, int count) {
        Table* current = root_.get();
        for (int i = 0; i < count; ++i) {
            Entry& entry = current->entries[path.at(i)];
            if (entry.isArray) {
                current = entry.array.back().get();
            } else if (entry.table) {
                current = entry.table.get();
            } else if (entry.scalar.isValid()) {
                fail(rlTr("studio", "'%1' is not a table").arg(path.mid(0, i + 1).join('.')));
            } else {
                entry.table = std::make_shared<Table>();
                current = entry.table.get();
            }
        }
        return current;
    }

    void ensureTable(QVariantMap&, const QStringList& path) { descend(path, path.size()); }

    void appendTable(QVariantMap&, const QStringList& path) {
        Table* parent = descend(path, path.size() - 1);
        Entry& entry = parent->entries[path.last()];
        if (entry.table || entry.scalar.isValid()) {
            fail(rlTr("studio", "'%1' redefined as an array of tables").arg(path.join('.')));
        }
        entry.isArray = true;
        entry.array.push_back(std::make_shared<Table>());
    }

    void assign(QVariantMap&, const QStringList& path, const QVariant& value, int) {
        Table* parent = descend(path, path.size() - 1);
        if (parent->entries.contains(path.last())) {
            fail(rlTr("studio", "duplicate key '%1'").arg(path.join('.')));
        }
        parent->entries[path.last()].scalar = value;
    }

    static QVariantMap toVariant(const Table& table) {
        QVariantMap map;
        for (auto it = table.entries.constBegin(); it != table.entries.constEnd(); ++it) {
            const Entry& entry = it.value();
            if (entry.isArray) {
                QVariantList list;
                for (const TablePtr& item : entry.array) {
                    list.append(toVariant(*item));
                }
                map.insert(it.key(), list);
            } else if (entry.table) {
                map.insert(it.key(), toVariant(*entry.table));
            } else {
                map.insert(it.key(), entry.scalar);
            }
        }
        return map;
    }

    TablePtr root_ = std::make_shared<Table>();

    static constexpr int kMaxArrayDepth = 64;

    const QString& text_;
    int pos_ = 0;
    int depth_ = 0;
};

QVariant lookup(const QVariantMap& root, const QString& dottedKey) {
    QVariant current = root;
    for (const QString& part : dottedKey.split(QLatin1Char('.'))) {
        if (current.type() != QVariant::Map) {
            return {};
        }
        current = current.toMap().value(part);
    }
    return current;
}

}  // namespace

ProjectFile ProjectFile::load(const QString& path, QString* error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = rlTr("studio", "Cannot open %1: %2").arg(path, file.errorString());
        }
        return {};
    }
    return parse(QString::fromUtf8(file.readAll()), path, error);
}

ProjectFile ProjectFile::parse(const QString& text, const QString& path, QString* error) {
    ProjectFile result;
    result.path_ = QFileInfo(path).absoluteFilePath();
    try {
        Reader reader(text);
        result.root_ = reader.parse();
        result.valid_ = true;
    } catch (const std::exception& e) {
        if (error) {
            *error = QStringLiteral("%1: %2").arg(QFileInfo(path).fileName(),
                                                  QString::fromUtf8(e.what()));
        }
    }
    return result;
}

QString ProjectFile::directory() const {
    return QFileInfo(path_).absolutePath();
}

QVariant ProjectFile::value(const QString& dottedKey) const {
    return lookup(root_, dottedKey);
}

QString ProjectFile::resolve(const QString& relativePath) const {
    return QDir::cleanPath(QDir(directory()).absoluteFilePath(relativePath));
}

}  // namespace rl::sdk
