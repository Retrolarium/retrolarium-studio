#pragma once

#include "sdk/types.hpp"

#include <QString>

class QSettings;

namespace rl::runtime {

// What the kit asks consent for: running the scripts (asset split, checks,
// builds) of a game repository it downloaded, at one commit and content.
struct ConsentRequest {
    QString repository;     // "smb1_src"
    QString commit;         // 40 hex digits
    QString contentSha256;  // the kit's hash of the repository's files
    QString url;            // https://github.com/<org>/<repository>/tree/<commit>
};

// Consent to run a downloaded repository's own scripts, asked once per
// repository and content. Without it the kit exits with code 3 and prints
// one JSON object on stdout:
//   {"schema": "retrolarium.consent-needed", "schema_version": 1,
//    "repository": ..., "commit": ..., "content_sha256": ..., "url": ...}
// The studio asks the user, keeps the answer in its settings
// (consent/<repository>/<content_sha256> = true) and passes every hash
// consented to, comma-separated, in RETROLARIUM_ALLOW_REPOSITORY_SCRIPTS on
// each kit run; then it runs the same command again.
class RepositoryConsent final {
public:
    static constexpr int kExitCode = 3;
    static constexpr const char* kEnvironmentVariable = "RETROLARIUM_ALLOW_REPOSITORY_SCRIPTS";

    // True, with `request` filled, when a finished kit run asks for consent.
    static bool parse(const sdk::ProcessResult& result, ConsentRequest* request);
    // Whether the user has consented to this repository and content.
    static bool allows(QSettings& settings, const ConsentRequest& request);
    static void record(QSettings& settings, const ConsentRequest& request);
    // Every content hash consented to, sorted and comma-separated.
    static QString allowed(QSettings& settings);
    // Sets RETROLARIUM_ALLOW_REPOSITORY_SCRIPTS for a kit command: the hashes
    // in the settings and any the studio's own environment already names.
    static void apply(QSettings& settings, sdk::ProcessSpec* spec);
};

}  // namespace rl::runtime
