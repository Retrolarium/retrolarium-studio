#pragma once

#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace rl::sdk {

// A Retrolarium project file (.rlj, TOML). The studio reads only enough to pick
// the plugin ([project] platform, profile, source_mode); the plugin reads and
// validates everything else.
//
// The reader supports the TOML subset project files use: tables, arrays of
// tables, dotted table names, basic and literal strings, integers, floats,
// booleans and (multi-line) arrays of those. Unsupported syntax is an error,
// never silently skipped.
class ProjectFile {
public:
    static ProjectFile load(const QString& path, QString* error);
    static ProjectFile parse(const QString& text, const QString& path, QString* error);

    bool isValid() const { return valid_; }
    QString path() const { return path_; }
    QString directory() const;

    const QVariantMap& root() const { return root_; }
    // Dotted lookup: value("project.profile").
    QVariant value(const QString& dottedKey) const;
    QString string(const QString& dottedKey) const { return value(dottedKey).toString(); }
    QVariantList list(const QString& dottedKey) const { return value(dottedKey).toList(); }

    // Absolute path of a file named relative to the project directory.
    QString resolve(const QString& relativePath) const;

private:
    bool valid_ = false;
    QString path_;
    QVariantMap root_;
};

}  // namespace rl::sdk
