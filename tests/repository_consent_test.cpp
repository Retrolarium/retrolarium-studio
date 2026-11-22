// Consent to run a downloaded game repository's scripts: the kit's request
// (exit code 3, one retrolarium.consent-needed object on stdout) is
// recognised and anything else is not, a consent is kept per repository and
// content hash, and every hash consented to reaches the kit's environment.
// Needs no project.
//   repository_consent_test

#include "runtime/repository_consent.hpp"

#include <QCoreApplication>
#include <QSettings>
#include <QTemporaryDir>

#include <iostream>

using rl::runtime::ConsentRequest;
using rl::runtime::RepositoryConsent;

namespace {

int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

const QByteArray kCommit = QByteArray(40, 'a');
const QByteArray kHash = QByteArray(64, 'b');
const QByteArray kOtherHash = QByteArray(64, 'c');

QByteArray answer(const QByteArray& repository, const QByteArray& hash, const QByteArray& url) {
    return "{\"schema\": \"retrolarium.consent-needed\", \"schema_version\": 1, \"repository\": \"" + repository +
           "\", \"commit\": \"" + kCommit + "\", \"content_sha256\": \"" + hash + "\", \"url\": \"" + url + "\"}\n";
}

QByteArray url(const QByteArray& repository) {
    return "https://github.com/RetrolariumLab/" + repository + "/tree/" + kCommit;
}

rl::sdk::ProcessResult result(int exitCode, const QByteArray& standardOutput) {
    rl::sdk::ProcessResult r;
    r.started = true;
    r.exitCode = exitCode;
    r.standardOutput = standardOutput;
    return r;
}

}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    ConsentRequest request;
    expect(RepositoryConsent::parse(result(3, answer("smb1_src", kHash, url("smb1_src"))), &request),
           "the kit's request is recognised");
    expect(request.repository == QLatin1String("smb1_src") && request.commit == QLatin1String(kCommit) &&
               request.contentSha256 == QLatin1String(kHash) && request.url == QLatin1String(url("smb1_src")),
           "the request's fields are read");

    ConsentRequest ignored;
    expect(!RepositoryConsent::parse(result(0, answer("smb1_src", kHash, url("smb1_src"))), &ignored),
           "only exit code 3 asks");
    expect(!RepositoryConsent::parse(result(3, "{\"status\": \"ready\"}"), &ignored), "another answer does not ask");
    expect(!RepositoryConsent::parse(result(3, "not json"), &ignored), "a failure's text does not ask");
    expect(!RepositoryConsent::parse(result(3, answer("../smb1_src", kHash, url("smb1_src"))), &ignored),
           "a repository name that is not one settings key is refused");
    expect(!RepositoryConsent::parse(result(3, answer("smb1_src", "b,c", url("smb1_src"))), &ignored),
           "a hash that is not 64 hex digits is refused");
    expect(!RepositoryConsent::parse(result(3, answer("smb1_src", kHash, "https://example.com/smb1_src")), &ignored),
           "a link that is not a GitHub commit is refused");
    rl::sdk::ProcessResult cancelled = result(3, answer("smb1_src", kHash, url("smb1_src")));
    cancelled.cancelled = true;
    expect(!RepositoryConsent::parse(cancelled, &ignored), "a cancelled run does not ask");

    QTemporaryDir dir;
    QSettings settings(dir.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
    expect(!RepositoryConsent::allows(settings, request), "nothing is consented to at first");
    expect(RepositoryConsent::allowed(settings).isEmpty(), "no hashes at first");

    qunsetenv(RepositoryConsent::kEnvironmentVariable);
    rl::sdk::ProcessSpec spec;
    RepositoryConsent::apply(settings, &spec);
    expect(spec.environment.contains(QString::fromLatin1(RepositoryConsent::kEnvironmentVariable)) &&
               spec.environment.value(QString::fromLatin1(RepositoryConsent::kEnvironmentVariable)).isEmpty(),
           "the kit hears that nothing is consented to");

    RepositoryConsent::record(settings, request);
    expect(RepositoryConsent::allows(settings, request), "a consent is kept");
    expect(settings.value(QStringLiteral("consent/smb1_src/") + QLatin1String(kHash)).toBool(),
           "it is kept under consent/<repository>/<content_sha256>");
    ConsentRequest changed = request;
    changed.contentSha256 = QLatin1String(kOtherHash);
    expect(!RepositoryConsent::allows(settings, changed), "other content of the same repository asks again");

    ConsentRequest second;
    RepositoryConsent::parse(result(3, answer("flicky_src", kOtherHash, url("flicky_src"))), &second);
    RepositoryConsent::record(settings, second);
    expect(RepositoryConsent::allowed(settings) == QLatin1String(kHash + "," + kOtherHash),
           "every hash consented to is listed");

    rl::sdk::ProcessSpec again;
    RepositoryConsent::apply(settings, &again);
    expect(again.environment.value(QString::fromLatin1(RepositoryConsent::kEnvironmentVariable)) ==
               QLatin1String(kHash + "," + kOtherHash),
           "every hash consented to reaches the kit");

    const QByteArray inherited(64, 'd');
    qputenv(RepositoryConsent::kEnvironmentVariable, inherited);
    rl::sdk::ProcessSpec withEnvironment;
    RepositoryConsent::apply(settings, &withEnvironment);
    expect(withEnvironment.environment.value(QString::fromLatin1(RepositoryConsent::kEnvironmentVariable))
               .contains(QLatin1String(inherited)),
           "a hash the studio's environment names is passed on too");
    qunsetenv(RepositoryConsent::kEnvironmentVariable);

    if (failures != 0) return 1;
    std::cout << "repository consent: requests, kept consents and the kit's environment hold\n";
    return 0;
}
