#include "runtime/repository_consent.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSettings>
#include <QStringList>

namespace rl::runtime {

namespace {

const char* const kSchema = "retrolarium.consent-needed";
const char* const kGroup = "consent";

// A repository name stays one settings key part: no '/', no "." or "..".
bool validRepository(const QString& name) {
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]{0,99}$"));
    return pattern.match(name).hasMatch();
}

bool validCommit(const QString& commit) {
    static const QRegularExpression pattern(QStringLiteral("^[0-9a-fA-F]{40}$"));
    return pattern.match(commit).hasMatch();
}

// No comma either: the hashes travel as a comma-separated list.
bool validHash(const QString& hash) {
    static const QRegularExpression pattern(QStringLiteral("^[0-9a-fA-F]{64}$"));
    return pattern.match(hash).hasMatch();
}

bool validUrl(const QString& url) {
    static const QRegularExpression pattern(
        QStringLiteral("^https://github\\.com/[A-Za-z0-9._-]+/[A-Za-z0-9._-]+/tree/[0-9a-fA-F]{40}$"));
    return pattern.match(url).hasMatch();
}

QString key(const ConsentRequest& request) {
    return QStringLiteral("%1/%2/%3").arg(QString::fromLatin1(kGroup), request.repository, request.contentSha256);
}

}  // namespace

bool RepositoryConsent::parse(const sdk::ProcessResult& result, ConsentRequest* request) {
    if (!result.started || result.crashed || result.cancelled || result.exitCode != kExitCode) {
        return false;
    }
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(result.standardOutput.trimmed(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return false;
    }
    const QJsonObject object = document.object();
    if (object.value(QStringLiteral("schema")).toString() != QLatin1String(kSchema) ||
        object.value(QStringLiteral("schema_version")).toInt() != 1) {
        return false;
    }
    ConsentRequest parsed;
    parsed.repository = object.value(QStringLiteral("repository")).toString();
    parsed.commit = object.value(QStringLiteral("commit")).toString();
    parsed.contentSha256 = object.value(QStringLiteral("content_sha256")).toString();
    parsed.url = object.value(QStringLiteral("url")).toString();
    if (!validRepository(parsed.repository) || !validCommit(parsed.commit) || !validHash(parsed.contentSha256) ||
        !validUrl(parsed.url)) {
        return false;
    }
    if (request) {
        *request = parsed;
    }
    return true;
}

bool RepositoryConsent::allows(QSettings& settings, const ConsentRequest& request) {
    return validRepository(request.repository) && validHash(request.contentSha256) &&
           settings.value(key(request)).toBool();
}

void RepositoryConsent::record(QSettings& settings, const ConsentRequest& request) {
    if (!validRepository(request.repository) || !validHash(request.contentSha256)) {
        return;
    }
    settings.setValue(key(request), true);
    settings.sync();
}

QString RepositoryConsent::allowed(QSettings& settings) {
    QStringList hashes;
    settings.beginGroup(QString::fromLatin1(kGroup));
    const QStringList repositories = settings.childGroups();
    for (const QString& repository : repositories) {
        settings.beginGroup(repository);
        const QStringList keys = settings.childKeys();
        for (const QString& hash : keys) {
            if (validHash(hash) && settings.value(hash).toBool() && !hashes.contains(hash)) {
                hashes << hash;
            }
        }
        settings.endGroup();
    }
    settings.endGroup();
    hashes.sort();
    return hashes.join(QLatin1Char(','));
}

void RepositoryConsent::apply(QSettings& settings, sdk::ProcessSpec* spec) {
    if (!spec) {
        return;
    }
    QStringList hashes = allowed(settings).split(QLatin1Char(','), Qt::SkipEmptyParts);
    const QStringList inherited =
        qEnvironmentVariable(kEnvironmentVariable).split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (const QString& hash : inherited) {
        const QString trimmed = hash.trimmed();
        if (validHash(trimmed) && !hashes.contains(trimmed)) {
            hashes << trimmed;
        }
    }
    // Always set, also empty: the studio's settings say what runs.
    spec->environment.insert(QString::fromLatin1(kEnvironmentVariable), hashes.join(QLatin1Char(',')));
}

}  // namespace rl::runtime
