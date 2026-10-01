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
    flicky/src/flicky/plugin/bonus_model.cpp
    flicky/src/flicky/plugin/bonus_model.hpp
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
    flicky/src/flicky/plugin/playtest_model.cpp
    flicky/src/flicky/plugin/playtest_model.hpp
    flicky/src/flicky/plugin/semantics_models.cpp
    flicky/src/flicky/plugin/semantics_models.hpp
    flicky/src/flicky/plugin/sound_models.cpp
    flicky/src/flicky/plugin/sound_models.hpp
    flicky/src/flicky/plugin/sprite_models.cpp
    flicky/src/flicky/plugin/sprite_models.hpp
)
target_link_libraries(flicky_plugin PUBLIC flicky_data studio_sdk)
studio_enable_warnings(flicky_plugin)

# The build step of a Flicky project runs this writer through the kit
# (RETROLARIUM_FLICKY_LEVEL_WRITER); it must sit next to the studio.
add_executable(retrolarium-flicky-level-writer
    flicky/tools/level_writer_main.cpp
)
target_link_libraries(retrolarium-flicky-level-writer PRIVATE flicky_data)

# NES platform: the PPU master palette and palette packet layout. Header-only, Qt-free.
add_library(platform_nes INTERFACE)
target_include_directories(platform_nes INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/nes/src)

# Super Mario Bros. data: the NES resource bundle, the area catalog and the
# level renderer (ported from the original app, pinned by the golden test).
add_library(smb_data STATIC
    smb/src/smb/data/area_streams.cpp
    smb/src/smb/data/area_streams.hpp
    smb/src/smb/data/catalog.cpp
    smb/src/smb/data/catalog.hpp
    smb/src/smb/data/level_records.hpp
    smb/src/smb/data/resource_document.cpp
    smb/src/smb/data/resource_document.hpp
    smb/src/smb/render/level_image.cpp
    smb/src/smb/render/level_image.hpp
    smb/src/smb/render/level_renderer.cpp
    smb/src/smb/render/level_renderer.hpp
)
target_include_directories(smb_data PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/smb/src)
target_link_libraries(smb_data PUBLIC platform_nes Qt5::Core Qt5::Gui)
studio_enable_warnings(smb_data)

# Super Mario Bros. plugin: profile, document lists and editor models.
add_library(smb_plugin STATIC
    smb/src/smb/plugin/graphics_models.cpp
    smb/src/smb/plugin/graphics_models.hpp
    smb/src/smb/plugin/level_model.cpp
    smb/src/smb/plugin/level_model.hpp
    smb/src/smb/plugin/playtest_model.cpp
    smb/src/smb/plugin/playtest_model.hpp
    smb/src/smb/plugin/smb_plugin.cpp
    smb/src/smb/plugin/smb_plugin.hpp
    smb/src/smb/plugin/smb_session.cpp
    smb/src/smb/plugin/smb_session.hpp
)
target_link_libraries(smb_plugin PUBLIC smb_data studio_sdk)
studio_enable_warnings(smb_plugin)

# The renderer against the kit's output for every SMB1 JU area:
#   smb_level_renderer_golden_test plugins/smb/tests/smb1_ju_level_scenes.json
add_executable(smb_level_renderer_golden_test
    smb/tests/level_renderer_golden_test.cpp
)
target_link_libraries(smb_level_renderer_golden_test PRIVATE smb_data)

# Record insertion and removal against every SMB1 JU area:
#   smb_area_streams_test plugins/smb/tests/smb1_ju_level_scenes.json
add_executable(smb_area_streams_test
    smb/tests/area_streams_test.cpp
)
target_link_libraries(smb_area_streams_test PRIVATE smb_data)

# The one translation unit that names concrete plugins.
add_library(studio_builtin_plugins STATIC
    builtin_plugins.cpp
    builtin_plugins.hpp
)
target_include_directories(studio_builtin_plugins PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(studio_builtin_plugins PUBLIC flicky_plugin smb_plugin studio_sdk)
studio_enable_warnings(studio_builtin_plugins)
