# Platform and game plugins. They link studio_sdk and their own code only;
# nothing here may include shell, editor, widget or theme headers.

# Mega Drive platform: VDP pattern formats and CRAM colors. Qt-free.
add_library(platform_megadrive STATIC
    megadrive/src/megadrive/compression.cpp
    megadrive/src/megadrive/compression.hpp
    megadrive/src/megadrive/tiles.cpp
    megadrive/src/megadrive/tiles.hpp
    megadrive/src/megadrive/vdp.hpp
)
target_include_directories(platform_megadrive PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/megadrive/src)
target_link_libraries(platform_megadrive PRIVATE retrolarium::core)
studio_enable_warnings(platform_megadrive)

# Flicky data: documents, codecs, the source writer and the scene renderer.
# QtCore, plus QtGui for the renderer's QImage.
add_library(flicky_data STATIC
    flicky/src/flicky/data/collision.cpp
    flicky/src/flicky/data/collision.hpp
    flicky/src/flicky/data/graphics_document.cpp
    flicky/src/flicky/data/graphics_document.hpp
    flicky/src/flicky/data/level_document.cpp
    flicky/src/flicky/data/level_document.hpp
    flicky/src/flicky/data/level_source_writer.cpp
    flicky/src/flicky/data/level_source_writer.hpp
    flicky/src/flicky/data/semantics_document.cpp
    flicky/src/flicky/data/semantics_document.hpp
    flicky/src/flicky/data/sequences_document.cpp
    flicky/src/flicky/data/sequences_document.hpp
    flicky/src/flicky/data/sound_document.cpp
    flicky/src/flicky/data/sound_document.hpp
    flicky/src/flicky/render/scene_renderer.cpp
    flicky/src/flicky/render/scene_renderer.hpp
)
target_include_directories(flicky_data PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/flicky/src)
target_link_libraries(flicky_data PUBLIC platform_megadrive Qt5::Core Qt5::Gui)
studio_enable_warnings(flicky_data)

# Flicky plugin: profile, document lists and editor models over flicky_data.
add_library(flicky_plugin STATIC
    flicky/src/flicky/plugin/flicky_plugin.cpp
    flicky/src/flicky/plugin/flicky_plugin.hpp
    flicky/src/flicky/plugin/flicky_session.cpp
    flicky/src/flicky/plugin/flicky_session.hpp
    flicky/src/flicky/plugin/graphics_models.cpp
    flicky/src/flicky/plugin/graphics_models.hpp
    flicky/src/flicky/plugin/levels_model.cpp
    flicky/src/flicky/plugin/levels_model.hpp
    flicky/src/flicky/plugin/objects.cpp
    flicky/src/flicky/plugin/objects.hpp
    flicky/src/flicky/plugin/semantics_models.cpp
    flicky/src/flicky/plugin/semantics_models.hpp
)
target_link_libraries(flicky_plugin PUBLIC flicky_data studio_sdk)
studio_enable_warnings(flicky_plugin)

# The build step of a Flicky project runs this writer through the kit
# (RETROLARIUM_FLICKY_LEVEL_WRITER); it must sit next to the studio.
add_executable(retrolarium-flicky-level-writer
    flicky/tools/level_writer_main.cpp
)
target_link_libraries(retrolarium-flicky-level-writer PRIVATE flicky_data)

# The one translation unit that names concrete plugins.
add_library(studio_builtin_plugins STATIC
    builtin_plugins.cpp
    builtin_plugins.hpp
)
target_include_directories(studio_builtin_plugins PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(studio_builtin_plugins PUBLIC flicky_plugin studio_sdk)
studio_enable_warnings(studio_builtin_plugins)
