#include "runtime/mame_config.hpp"

#include "sdk/i18n.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QProcess>
#include <QSaveFile>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

namespace rl::runtime::mame {

namespace {

constexpr int kListTimeoutMs = 15000;

void writeWarnings(QXmlStreamWriter& xml, const QVector<Feature>& features, const QString& indent) {
    const QString now = QString::number(QDateTime::currentSecsSinceEpoch());
    xml.writeCharacters(QStringLiteral("\n") + indent);
    xml.writeStartElement(QStringLiteral("ui_warnings"));
    xml.writeAttribute(QStringLiteral("launched"), now);
    xml.writeAttribute(QStringLiteral("warned"), now);
    for (const Feature& feature : features) {
        xml.writeCharacters(QStringLiteral("\n") + indent + QStringLiteral("    "));
        xml.writeEmptyElement(QStringLiteral("feature"));
        xml.writeAttribute(QStringLiteral("device"), feature.device);
        xml.writeAttribute(QStringLiteral("type"), feature.type);
        xml.writeAttribute(QStringLiteral("status"), feature.status);
    }
    xml.writeCharacters(QStringLiteral("\n") + indent);
    xml.writeEndElement();
}

}  // namespace

QVector<Feature> warnedFeatures(const QString& program, const QString& system, QString* error) {
    static QHash<QString, QVector<Feature>> cache;
    const QString key = program + QLatin1Char('|') + system;
    const auto found = cache.constFind(key);
    if (found != cache.constEnd()) {
        return *found;
    }
    QProcess process;
    process.setProgram(program);
    process.setArguments({QStringLiteral("-listxml"), system});
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start();
    if (!process.waitForFinished(kListTimeoutMs) || process.exitCode() != 0) {
        process.kill();
        // Remembered too: asking again would block every launch as long.
        cache.insert(key, {});
        if (error) {
            *error = rlTr("studio", "MAME did not describe %1").arg(system);
        }
        return {};
    }
    // The system's own <machine> element; devices follow as machines of their own.
    QVector<Feature> features;
    QXmlStreamReader xml(process.readAllStandardOutput());
    int depth = 0;
    bool inSystem = false;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            ++depth;
            if (depth == 2 && xml.name() == QLatin1String("machine")) {
                inSystem = xml.attributes().value(QStringLiteral("name")) == system;
            } else if (inSystem && depth == 3 && xml.name() == QLatin1String("feature")) {
                const QString status = xml.attributes().value(QStringLiteral("status")).toString();
                if (status == QLatin1String("imperfect") || status == QLatin1String("unemulated")) {
                    features.push_back({system, xml.attributes().value(QStringLiteral("type")).toString(), status});
                }
            }
        } else if (xml.isEndElement()) {
            if (depth == 2 && inSystem) {
                break;
            }
            --depth;
        }
    }
    if (xml.hasError()) {
        cache.insert(key, {});
        if (error) {
            *error = xml.errorString();
        }
        return {};
    }
    cache.insert(key, features);
    return features;
}

bool acknowledgeWarnings(const QString& configDirectory, const QString& system, const QVector<Feature>& features,
                         QString* error) {
    const QString path = QDir(configDirectory).filePath(system + QStringLiteral(".cfg"));
    QByteArray source;
    {
        QFile file(path);
        if (file.open(QIODevice::ReadOnly)) {
            source = file.readAll();
        }
    }
    if (source.trimmed().isEmpty()) {
        source = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<mameconfig version=\"10\">\n</mameconfig>\n";
    }

    QByteArray result;
    QXmlStreamReader in(source);
    QXmlStreamWriter out(&result);
    int depth = 0;
    bool inSystem = false;
    bool wroteSystem = false;
    int skipDepth = 0;  // inside an old <ui_warnings>, which is replaced
    while (!in.atEnd()) {
        in.readNext();
        if (in.hasError()) {
            break;
        }
        if (skipDepth > 0) {
            if (in.isStartElement()) ++skipDepth;
            if (in.isEndElement()) --skipDepth;
            continue;
        }
        if (in.isStartElement()) {
            ++depth;
            if (depth == 2 && in.name() == QLatin1String("system")) {
                inSystem = in.attributes().value(QStringLiteral("name")) == system;
            } else if (inSystem && depth == 3 && in.name() == QLatin1String("ui_warnings")) {
                skipDepth = 1;
                --depth;
                continue;
            }
        } else if (in.isEndElement()) {
            if (depth == 2 && inSystem) {
                writeWarnings(out, features, QStringLiteral("        "));
                out.writeCharacters(QStringLiteral("\n    "));
                inSystem = false;
                wroteSystem = true;
            } else if (depth == 1 && !wroteSystem) {
                out.writeCharacters(QStringLiteral("    "));
                out.writeStartElement(QStringLiteral("system"));
                out.writeAttribute(QStringLiteral("name"), system);
                writeWarnings(out, features, QStringLiteral("        "));
                out.writeCharacters(QStringLiteral("\n    "));
                out.writeEndElement();
                out.writeCharacters(QStringLiteral("\n"));
                wroteSystem = true;
            }
            --depth;
        }
        out.writeCurrentToken(in);
    }
    if (in.hasError() || !wroteSystem) {
        if (error) {
            *error = rlTr("studio", "%1 is not a MAME configuration I can update").arg(QDir::toNativeSeparators(path));
        }
        return false;
    }
    if (!QDir().mkpath(configDirectory)) {
        if (error) {
            *error = rlTr("studio", "Cannot create %1").arg(QDir::toNativeSeparators(configDirectory));
        }
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(result) != result.size() || !file.commit()) {
        if (error) {
            *error = file.errorString();
        }
        return false;
    }
    return true;
}

bool enableSkipWarnings(const QString& workingDirectory, bool* changed, QString* error) {
    if (changed) *changed = false;
    const QDir directory(workingDirectory);
    QString path = directory.filePath(QStringLiteral("ui.ini"));
    if (!QFile::exists(path) && QFile::exists(directory.filePath(QStringLiteral("ini/ui.ini")))) {
        path = directory.filePath(QStringLiteral("ini/ui.ini"));
    }
    QByteArray text;
    {
        QFile file(path);
        if (file.open(QIODevice::ReadOnly)) text = file.readAll();
    }
    // The option's line ("skip_warnings   1"), whatever its spacing.
    static const QByteArray kOption("skip_warnings");
    static const QByteArray kLine("skip_warnings             1");
    QList<QByteArray> lines = text.split('\n');
    bool found = false;
    for (QByteArray& line : lines) {
        const QByteArray trimmed = line.trimmed();
        const bool option =
            trimmed.startsWith(kOption) && (trimmed.size() == kOption.size() || trimmed.at(kOption.size()) == ' ' ||
                                            trimmed.at(kOption.size()) == '\t');
        if (!option) continue;
        found = true;
        if (trimmed.simplified() == "skip_warnings 1") return true;
        line = kLine + (line.endsWith('\r') ? QByteArray("\r") : QByteArray());
    }
    if (!found) {
        // A last empty element is the file's final newline.
        if (!lines.isEmpty() && lines.last().isEmpty()) lines.removeLast();
        lines << kLine << QByteArray();
    }
    const QByteArray result = lines.join('\n');
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(result) != result.size() || !file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    if (changed) *changed = true;
    return true;
}

}  // namespace rl::runtime::mame
