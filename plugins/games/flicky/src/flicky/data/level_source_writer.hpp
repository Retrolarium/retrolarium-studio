#pragma once

#include <QString>

namespace retrolarium {

class FlickyLevelDocument;

// The staged tree is disposable build output, never the pinned source snapshot.
void writeFlickyLevelSources(const FlickyLevelDocument &document,
                             const QString &stagedSource);

} // namespace retrolarium
