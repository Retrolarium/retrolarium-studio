#include "flicky/data/level_document.hpp"
#include "flicky/data/level_source_writer.hpp"

#include <QCoreApplication>
#include <QStringList>

#include <cstdio>

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    const auto arguments = application.arguments();
    if (arguments.size() != 3) {
        std::fprintf(stderr, "usage: retrolarium-flicky-level-writer levels.json staged-source\n");
        return 2;
    }
    try {
        const auto document = rl::flicky::FlickyLevelDocument::load(arguments[1]);
        rl::flicky::writeFlickyLevelSources(document, arguments[2]);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
    return 0;
}
