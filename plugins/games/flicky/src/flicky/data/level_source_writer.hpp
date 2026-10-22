#pragma once

#include <QString>

namespace rl::flicky {

class FlickyLevelDocument;

// The staged tree is disposable build output, never the pinned source snapshot.
void writeFlickyLevelSources(const FlickyLevelDocument& document, const QString& stagedSource);

}  // namespace rl::flicky
