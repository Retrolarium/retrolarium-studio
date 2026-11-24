#include "sdk/project_file.hpp"

#include "sdk/i18n.hpp"
#include "sdk/project_writer.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <QMap>
#include <QSet>

#include <memory>
#include <stdexcept>
#include <vector>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

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
        QStringList table;        // current table path
        bool arrayTable = false;  // current table is the last element of an array
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
    QChar peekAt(int offset) const { return pos_ + offset < text_.size() ? text_.at(pos_ + offset) : QChar(); }

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
            if (c == QLatin1Char(' ') || c == QLatin1Char('\t') || c == QLatin1Char('\r') || c == QLatin1Char('\n')) {
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
                case 'n':
                    result.append(QLatin1Char('\n'));
                    break;
                case 't':
                    result.append(QLatin1Char('\t'));
                    break;
                case 'r':
                    result.append(QLatin1Char('\r'));
                    break;
                case 'b':
                    result.append(QChar(8));
                    break;
                case 'f':
                    result.append(QChar(12));
                    break;
                case '"':
                    result.append(QLatin1Char('"'));
                    break;
                case '\\':
                    result.append(QLatin1Char('\\'));
                    break;
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
            if (d == QLatin1Char(',') || d == QLatin1Char(']') || d == QLatin1Char('#') || d == QLatin1Char('\n') ||
                d == QLatin1Char('\r') || d == QLatin1Char(' ') || d == QLatin1Char('\t')) {
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
        const qlonglong integer =
            token.startsWith(QLatin1String("0x")) ? token.mid(2).toLongLong(&ok, 16) : token.toLongLong(&ok, 10);
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
        // Dotted keys nest tables as deep as they go; converting them later
        // recurses as deep, so the depth is bounded as arrays' is.
        if (path.size() > kMaxArrayDepth) {
            fail(rlTr("studio", "keys nested deeper than %1 tables").arg(kMaxArrayDepth));
        }
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

    // A [table] header defines its table once (TOML); one made on the way
    // to a deeper header ([a] after [a.b]) is still free to define.
    void ensureTable(QVariantMap&, const QStringList& path) {
        const Table* table = descend(path, path.size());
        if (defined_.contains(table)) {
            fail(rlTr("studio", "table '%1' defined twice").arg(path.join('.')));
        }
        defined_.insert(table);
    }

    void appendTable(QVariantMap&, const QStringList& path) {
        Table* parent = descend(path, path.size() - 1);
        Entry& entry = parent->entries[path.last()];
        if (entry.table || entry.scalar.isValid()) {
            fail(rlTr("studio", "'%1' redefined as an array of tables").arg(path.join('.')));
        }
        entry.isArray = true;
        entry.array.push_back(std::make_shared<Table>());
        defined_.insert(entry.array.back().get());
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
    // The tables a header has defined.
    QSet<const Table*> defined_;

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

bool within(const QString& path, const QString& root) {
#ifdef Q_OS_WIN
    const auto sensitivity = Qt::CaseInsensitive;
#else
    const auto sensitivity = Qt::CaseSensitive;
#endif
    const QString prefix = root.endsWith(QLatin1Char('/')) ? root : root + QLatin1Char('/');
    return path.compare(root, sensitivity) == 0 || path.startsWith(prefix, sensitivity);
}

void validatePortableComponents(const QString& relativePath) {
    static const QSet<QString> devices = {QStringLiteral("CON"), QStringLiteral("PRN"),    QStringLiteral("AUX"),
                                          QStringLiteral("NUL"), QStringLiteral("CONIN$"), QStringLiteral("CONOUT$")};
    for (const QString& component : relativePath.split(QLatin1Char('/'))) {
        if (component == QLatin1String(".") || component == QLatin1String("..")) continue;
        const QString base = component.section(QLatin1Char('.'), 0, 0).toUpper();
        const bool numberedDevice = base.size() == 4 && base.at(3).isDigit() && base.at(3) != QLatin1Char('0') &&
                                    (base.startsWith(QLatin1String("COM")) || base.startsWith(QLatin1String("LPT")));
        if (component.endsWith(QLatin1Char('.')) || component.endsWith(QLatin1Char(' ')) || devices.contains(base) ||
            numberedDevice) {
            throw ParseError(
                rlTr("studio", "The name '%1' is not supported by Windows file systems.").arg(component).toStdString());
        }
    }
}

#ifdef Q_OS_WIN
std::wstring nativeAbsolute(const QString& path) {
    QString native = QDir::toNativeSeparators(path);
    if (!native.startsWith(QStringLiteral("\\\\?\\"))) {
        native = native.startsWith(QStringLiteral("\\\\")) ? QStringLiteral("\\\\?\\UNC\\") + native.mid(2)
                                                           : QStringLiteral("\\\\?\\") + native;
    }
    return native.toStdWString();
}

bool existsOrLink(const QString& path) {
    return GetFileAttributesW(nativeAbsolute(path).c_str()) != INVALID_FILE_ATTRIBUTES;
}

QString canonicalExisting(const QString& path) {
    // Qt 5's canonicalFilePath does not reliably expand NTFS junctions.
    // Follow the actual handle, including long paths and UNC paths.
    const HANDLE handle = CreateFileW(nativeAbsolute(path).c_str(), FILE_READ_ATTRIBUTES,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                      FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) throw ParseError("A project path cannot be resolved.");
    using FinalPath = DWORD(WINAPI*)(HANDLE, LPWSTR, DWORD, DWORD);
    static const auto finalPath =
        reinterpret_cast<FinalPath>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetFinalPathNameByHandleW"));
    const DWORD required = finalPath ? finalPath(handle, nullptr, 0, 0) : 0;
    std::vector<wchar_t> buffer(required + 1);
    const DWORD written = required ? finalPath(handle, buffer.data(), static_cast<DWORD>(buffer.size()), 0) : 0;
    CloseHandle(handle);
    if (!written || written >= buffer.size()) {
        throw ParseError(
            rlTr("studio", "The file system cannot verify project paths. Open the project from a local disk.")
                .toStdString());
    }
    QString result = QString::fromWCharArray(buffer.data(), static_cast<int>(written));
    if (result.startsWith(QStringLiteral("\\\\?\\UNC\\"))) result = QStringLiteral("\\\\") + result.mid(8);
    else if (result.startsWith(QStringLiteral("\\\\?\\"))) result.remove(0, 4);
    return QDir::fromNativeSeparators(result);
}
#else
bool existsOrLink(const QString& path) { return QFileInfo::exists(path) || QFileInfo(path).isSymLink(); }
QString canonicalExisting(const QString& path) { return QFileInfo(path).canonicalFilePath(); }
#endif

// Resolve the nearest existing ancestor for new output files. Dangling
// symlinks/junctions are refused instead of treated as new directories.
QString realPath(QString path) {
    // Windows does not collapse dot components in extended-length (\\?\) paths.
    // Normalize before any platform call so repository boundaries stay at <game>.
    path = QDir::cleanPath(QDir::fromNativeSeparators(path));
    QStringList missing;
    while (!existsOrLink(path)) {
        const QFileInfo info(path);
        if (info.isSymLink()) throw ParseError("A project path contains a dangling link.");
        missing.prepend(info.fileName());
        const QString parent = info.absolutePath();
        if (parent == path) throw ParseError("A project path cannot be resolved.");
        path = parent;
    }
    const QString canonical = canonicalExisting(path);
    if (canonical.isEmpty()) throw ParseError("A project path cannot be resolved.");
    return QDir::cleanPath(QDir(canonical).filePath(missing.join(QLatin1Char('/'))));
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
        // Game descriptors are metadata for Kit, not editable projects. Their
        // source fields describe game inputs and need not be project documents.
        if (result.string(QStringLiteral("schema")) != QLatin1String("retrolarium.game")) {
            result.canonicalDirectory_ = realPath(result.directory());
            result.boundary_ = result.canonicalDirectory_;
            if (!result.string(QStringLiteral("source.repository")).isEmpty()) {
                result.boundary_ = realPath(QDir(result.canonicalDirectory_).absoluteFilePath(QStringLiteral("../..")));
            }
            QString why;
            if (!result.validateEditablePaths(&why)) throw ParseError(why.toStdString());
        }
        result.valid_ = true;
    } catch (const std::exception& e) {
        if (error) {
            *error = QStringLiteral("%1: %2").arg(QFileInfo(path).fileName(), QString::fromUtf8(e.what()));
        }
    }
    return result;
}

QString ProjectFile::directory() const { return QFileInfo(path_).absolutePath(); }

QVariant ProjectFile::value(const QString& dottedKey) const { return lookup(root_, dottedKey); }

QString ProjectFile::resolve(const QString& relativePath) const {
    if (relativePath.isEmpty() || QDir::isAbsolutePath(relativePath) || relativePath.contains(QLatin1Char(':')) ||
        relativePath.contains(QLatin1Char('\\'))) {
        throw ParseError("A project path must be relative to the project.");
    }
    validatePortableComponents(relativePath);
    const QString own = canonicalDirectory_.isEmpty() ? realPath(directory()) : canonicalDirectory_;
    const QString boundary = boundary_.isEmpty() ? own : boundary_;
    const QString repository = string(QStringLiteral("source.repository"));
    if (!repository.isEmpty()) {
        const QString workspace = QFileInfo(own).absolutePath();
        if (repository != QLatin1String("../..") || QFileInfo(workspace).fileName() != QLatin1String(".retrolarium")) {
            throw ParseError("A repository project must be under <game>/.retrolarium/<revision>.");
        }
        if (!within(own, boundary)) throw ParseError("The project is outside its game repository.");
    }
    const QString lexical = QDir::cleanPath(QDir(own).absoluteFilePath(relativePath));
    const QString canonical = realPath(lexical);
    if (!within(lexical, boundary) || !within(canonical, boundary)) {
        throw ParseError("A project path leads outside its project tree.");
    }
    return lexical;
}

bool ProjectFile::validateEditablePaths(QString* error) const {
    try {
        // Validate the boundary even for a project with no document tabs yet.
        resolve(QStringLiteral("."));
        for (const QVariant& value : list(QStringLiteral("workspace.tabs"))) {
            const QVariantMap tab = value.toMap();
            if (tab.contains(QStringLiteral("document"))) resolve(tab.value(QStringLiteral("document")).toString());
        }
        for (const char* key : {"source.rom", "source.original", "source.manifest", "source.disassembly"}) {
            const QString relative = string(QString::fromLatin1(key));
            if (!relative.isEmpty()) resolve(relative);
        }
        return true;
    } catch (const std::exception& exception) {
        if (error) *error = QString::fromUtf8(exception.what());
        return false;
    }
}

bool ProjectFile::validateWritePath(const QString& path, QString* error) const {
    try {
        if (!QDir::isAbsolutePath(path)) throw ParseError("A document write path must be absolute.");
        const QString normalized = QDir::fromNativeSeparators(path);
        if (QDir::cleanPath(normalized) != normalized) {
            throw ParseError("A document write path must not contain parent or redundant components.");
        }
        resolve(QDir(canonicalDirectory_).relativeFilePath(path));
        return true;
    } catch (const std::exception& exception) {
        if (error) *error = QString::fromUtf8(exception.what());
        return false;
    }
}

bool ProjectFile::writeFile(const QString& path, const QByteArray& data, QString* error) const {
    if (!validateWritePath(path, error)) return false;
    return writeFileWithin(boundary_, path, data, error);
}

}  // namespace rl::sdk
