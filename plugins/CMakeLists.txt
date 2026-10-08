# Platform and game plugins. They link studio_sdk and their own code only;
# nothing here may include shell, editor, widget or theme headers.

# Mega Drive platform: VDP pattern formats and CRAM colors. Qt-free.
add_library(platform_megadrive STATIC
    platforms/megadrive/src/megadrive/compression.cpp
    platforms/megadrive/src/megadrive/compression.hpp
    platforms/megadrive/src/megadrive/tiles.cpp
    platforms/megadrive/src/megadrive/tiles.hpp
    platforms/megadrive/src/megadrive/vdp.hpp
)
target_include_directories(platform_megadrive PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/platforms/megadrive/src)
target_link_libraries(platform_megadrive PRIVATE retrolarium::core)
studio_enable_warnings(platform_megadrive)

# Flicky data: documents, codecs, the source writer and the scene renderer.
# QtCore, plus QtGui for the renderer's QImage.
add_library(flicky_data STATIC
    games/flicky/src/flicky/data/collision.cpp
    games/flicky/src/flicky/data/collision.hpp
    games/flicky/src/flicky/data/graphics_document.cpp
    games/flicky/src/flicky/data/graphics_document.hpp
    games/flicky/src/flicky/data/level_document.cpp
    games/flicky/src/flicky/data/level_document.hpp
    games/flicky/src/flicky/data/level_source_writer.cpp
    games/flicky/src/flicky/data/level_source_writer.hpp
    games/flicky/src/flicky/data/semantics_document.cpp
    games/flicky/src/flicky/data/semantics_document.hpp
    games/flicky/src/flicky/data/sequences_document.cpp
    games/flicky/src/flicky/data/sequences_document.hpp
    games/flicky/src/flicky/data/sound_document.cpp
    games/flicky/src/flicky/data/sound_document.hpp
    games/flicky/src/flicky/render/scene_renderer.cpp
    games/flicky/src/flicky/render/scene_renderer.hpp
)
target_include_directories(flicky_data PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/games/flicky/src)
target_link_libraries(flicky_data PUBLIC platform_megadrive Qt5::Core Qt5::Gui)
studio_enable_warnings(flicky_data)

# Flicky plugin: profile, document lists and editor models over flicky_data.
add_library(flicky_plugin STATIC
    games/flicky/src/flicky/plugin/bonus_model.cpp
    games/flicky/src/flicky/plugin/bonus_model.hpp
    games/flicky/src/flicky/plugin/demo_model.cpp
    games/flicky/src/flicky/plugin/demo_model.hpp
    games/flicky/src/flicky/plugin/flicky_plugin.cpp
    games/flicky/src/flicky/plugin/flicky_plugin.hpp
    games/flicky/src/flicky/plugin/flicky_session.cpp
    games/flicky/src/flicky/plugin/flicky_session.hpp
    games/flicky/src/flicky/plugin/graphics_models.cpp
    games/flicky/src/flicky/plugin/graphics_models.hpp
    games/flicky/src/flicky/plugin/levels_model.cpp
    games/flicky/src/flicky/plugin/levels_model.hpp
    games/flicky/src/flicky/plugin/objects.cpp
    games/flicky/src/flicky/plugin/objects.hpp
    games/flicky/src/flicky/plugin/playtest_model.cpp
    games/flicky/src/flicky/plugin/playtest_model.hpp
    games/flicky/src/flicky/plugin/screen_model.cpp
    games/flicky/src/flicky/plugin/screen_model.hpp
    games/flicky/src/flicky/plugin/semantics_models.cpp
    games/flicky/src/flicky/plugin/semantics_models.hpp
    games/flicky/src/flicky/plugin/sound_models.cpp
    games/flicky/src/flicky/plugin/sound_models.hpp
    games/flicky/src/flicky/plugin/sprite_models.cpp
    games/flicky/src/flicky/plugin/sprite_models.hpp
)
target_link_libraries(flicky_plugin PUBLIC flicky_data studio_sdk)
studio_enable_warnings(flicky_plugin)

# The build step of a Flicky project runs this writer through the kit
# (RETROLARIUM_FLICKY_LEVEL_WRITER); the studio finds it in its tools folder.
add_executable(retrolarium-flicky-level-writer
    games/flicky/tools/level_writer_main.cpp
)
target_link_libraries(retrolarium-flicky-level-writer PRIVATE flicky_data)
install(TARGETS retrolarium-flicky-level-writer RUNTIME DESTINATION tools)

# NES platform: the PPU master palette and palette packet layout. Header-only, Qt-free.
add_library(platform_nes INTERFACE)
target_include_directories(platform_nes INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/platforms/nes/src)

# NES editors shared by NES games: the resource bundle (CHR banks, palette
# tables) and the pattern and palette-table models over it.
add_library(nes_editors STATIC
    platforms/nes/src/nes/bundle.cpp
    platforms/nes/src/nes/bundle.hpp
    platforms/nes/src/nes/editors.cpp
    platforms/nes/src/nes/editors.hpp
    platforms/nes/src/nes/sound_effect_model.cpp
    platforms/nes/src/nes/sound_effect_model.hpp
)
target_link_libraries(nes_editors PUBLIC platform_nes studio_sdk Qt5::Core Qt5::Gui)
studio_enable_warnings(nes_editors)

# Super Mario Bros. data: the NES resource bundle, the area catalog and the
# level renderer (ported from the original app, pinned by the golden test).
add_library(smb_data STATIC
    games/smb/src/smb/data/area_streams.cpp
    games/smb/src/smb/data/area_streams.hpp
    games/smb/src/smb/data/catalog.cpp
    games/smb/src/smb/data/catalog.hpp
    games/smb/src/smb/data/level_records.hpp
    games/smb/src/smb/data/music.cpp
    games/smb/src/smb/data/music.hpp
    games/smb/src/smb/data/resource_document.cpp
    games/smb/src/smb/data/resource_document.hpp
    games/smb/src/smb/data/screens.cpp
    games/smb/src/smb/data/screens.hpp
    games/smb/src/smb/render/level_image.cpp
    games/smb/src/smb/render/level_image.hpp
    games/smb/src/smb/render/level_renderer.cpp
    games/smb/src/smb/render/level_renderer.hpp
)
target_include_directories(smb_data PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/games/smb/src)
target_link_libraries(smb_data PUBLIC platform_nes Qt5::Core Qt5::Gui)
studio_enable_warnings(smb_data)

# Super Mario Bros. plugin: profile, document lists and editor models.
add_library(smb_plugin STATIC
    games/smb/src/smb/plugin/demo_model.cpp
    games/smb/src/smb/plugin/demo_model.hpp
    games/smb/src/smb/plugin/graphics_models.cpp
    games/smb/src/smb/plugin/graphics_models.hpp
    games/smb/src/smb/plugin/level_model.cpp
    games/smb/src/smb/plugin/level_model.hpp
    games/smb/src/smb/plugin/metatile_model.cpp
    games/smb/src/smb/plugin/metatile_model.hpp
    games/smb/src/smb/plugin/music_model.cpp
    games/smb/src/smb/plugin/music_model.hpp
    games/smb/src/smb/plugin/player_models.cpp
    games/smb/src/smb/plugin/player_models.hpp
    games/smb/src/smb/plugin/playtest_model.cpp
    games/smb/src/smb/plugin/playtest_model.hpp
    games/smb/src/smb/plugin/screen_model.cpp
    games/smb/src/smb/plugin/screen_model.hpp
    games/smb/src/smb/plugin/smb_plugin.cpp
    games/smb/src/smb/plugin/smb_plugin.hpp
    games/smb/src/smb/plugin/smb_session.cpp
    games/smb/src/smb/plugin/smb_session.hpp
    games/smb/src/smb/plugin/sound_effects.cpp
    games/smb/src/smb/plugin/sound_effects.hpp
    games/smb/src/smb/plugin/text_model.cpp
    games/smb/src/smb/plugin/text_model.hpp
    games/smb/src/smb/plugin/world_models.cpp
    games/smb/src/smb/plugin/world_models.hpp
)
target_link_libraries(smb_plugin PUBLIC nes_editors smb_data studio_sdk)
studio_enable_warnings(smb_plugin)

# The renderer against the kit's output for every SMB1 JU area.
add_executable(smb_level_renderer_golden_test
    games/smb/tests/level_renderer_golden_test.cpp
)
target_link_libraries(smb_level_renderer_golden_test PRIVATE smb_data)
studio_add_test(smb_level_renderer_golden_test games:smb1_src/tests/retrolarium/smb1_ju_level_scenes.json)

# Record insertion and removal against every SMB1 JU area.
add_executable(smb_area_streams_test
    games/smb/tests/area_streams_test.cpp
)
target_link_libraries(smb_area_streams_test PRIVATE smb_data)
studio_add_test(smb_area_streams_test games:smb1_src/tests/retrolarium/smb1_ju_level_scenes.json)

# Screen packets and text against the SMB1 JU screens.
add_executable(smb_screens_test
    games/smb/tests/screens_test.cpp
)
target_link_libraries(smb_screens_test PRIVATE smb_data)
studio_add_test(smb_screens_test games:smb1_src/tests/retrolarium/smb1_ju_screens.json)

# The music decoder against the kit's timelines for every SMB1 JU header.
add_executable(smb_music_test
    games/smb/tests/music_test.cpp
)
target_link_libraries(smb_music_test PRIVATE smb_data)
studio_add_test(smb_music_test games:smb1_src/tests/retrolarium/smb1_ju_music.json)

# Castle Excellent / Castlequest: the room document, its painter and the
# editor models.
add_library(castle_plugin STATIC
    games/castle/src/castle/data/levels.cpp
    games/castle/src/castle/data/levels.hpp
    games/castle/src/castle/data/music.cpp
    games/castle/src/castle/data/music.hpp
    games/castle/src/castle/plugin/animation_model.cpp
    games/castle/src/castle/plugin/animation_model.hpp
    games/castle/src/castle/plugin/castle_plugin.cpp
    games/castle/src/castle/plugin/castle_plugin.hpp
    games/castle/src/castle/plugin/castle_session.cpp
    games/castle/src/castle/plugin/castle_session.hpp
    games/castle/src/castle/plugin/cell_model.cpp
    games/castle/src/castle/plugin/cell_model.hpp
    games/castle/src/castle/plugin/demo_model.cpp
    games/castle/src/castle/plugin/demo_model.hpp
    games/castle/src/castle/plugin/map_model.cpp
    games/castle/src/castle/plugin/map_model.hpp
    games/castle/src/castle/plugin/music_models.cpp
    games/castle/src/castle/plugin/music_models.hpp
    games/castle/src/castle/plugin/room_model.cpp
    games/castle/src/castle/plugin/room_model.hpp
    games/castle/src/castle/plugin/screen_model.cpp
    games/castle/src/castle/plugin/screen_model.hpp
    games/castle/src/castle/plugin/sprite_model.cpp
    games/castle/src/castle/plugin/sprite_model.hpp
    games/castle/src/castle/plugin/table_models.cpp
    games/castle/src/castle/plugin/table_models.hpp
    games/castle/src/castle/render/room_painter.cpp
    games/castle/src/castle/render/room_painter.hpp
)
target_include_directories(castle_plugin PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/games/castle/src)
target_link_libraries(castle_plugin PUBLIC nes_editors studio_sdk)
studio_enable_warnings(castle_plugin)

# Doraemon: the World 1-3 maps, blocks and palettes of the resource bundle.
add_library(doraemon_plugin STATIC
    games/doraemon/src/doraemon/plugin/animation_model.cpp
    games/doraemon/src/doraemon/plugin/animation_model.hpp
    games/doraemon/src/doraemon/plugin/block_model.cpp
    games/doraemon/src/doraemon/plugin/block_model.hpp
    games/doraemon/src/doraemon/plugin/demo_model.cpp
    games/doraemon/src/doraemon/plugin/demo_model.hpp
    games/doraemon/src/doraemon/plugin/doraemon_plugin.cpp
    games/doraemon/src/doraemon/plugin/doraemon_plugin.hpp
    games/doraemon/src/doraemon/plugin/doraemon_session.cpp
    games/doraemon/src/doraemon/plugin/doraemon_session.hpp
    games/doraemon/src/doraemon/plugin/map_model.cpp
    games/doraemon/src/doraemon/plugin/music_model.cpp
    games/doraemon/src/doraemon/plugin/music_model.hpp
    games/doraemon/src/doraemon/plugin/map_model.hpp
    games/doraemon/src/doraemon/plugin/playtest_model.cpp
    games/doraemon/src/doraemon/plugin/playtest_model.hpp
    games/doraemon/src/doraemon/plugin/route_model.cpp
    games/doraemon/src/doraemon/plugin/route_model.hpp
    games/doraemon/src/doraemon/plugin/screen_model.cpp
    games/doraemon/src/doraemon/plugin/screen_model.hpp
    games/doraemon/src/doraemon/plugin/text_model.cpp
    games/doraemon/src/doraemon/plugin/text_model.hpp
    games/doraemon/src/doraemon/render/painter.cpp
    games/doraemon/src/doraemon/render/painter.hpp
)
target_include_directories(doraemon_plugin PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/games/doraemon/src)
target_link_libraries(doraemon_plugin PUBLIC nes_editors studio_sdk)
studio_enable_warnings(doraemon_plugin)

# Pac-Man: the maze, actor frames, screens and sound of the resource bundle.
add_library(pacman_plugin STATIC
    games/pacman/src/pacman/plugin/animation_model.cpp
    games/pacman/src/pacman/plugin/animation_model.hpp
    games/pacman/src/pacman/plugin/demo_model.cpp
    games/pacman/src/pacman/plugin/demo_model.hpp
    games/pacman/src/pacman/plugin/maze_model.cpp
    games/pacman/src/pacman/plugin/maze_model.hpp
    games/pacman/src/pacman/plugin/pacman_plugin.cpp
    games/pacman/src/pacman/plugin/pacman_plugin.hpp
    games/pacman/src/pacman/plugin/pacman_session.cpp
    games/pacman/src/pacman/plugin/pacman_session.hpp
    games/pacman/src/pacman/plugin/playtest_model.cpp
    games/pacman/src/pacman/plugin/playtest_model.hpp
    games/pacman/src/pacman/plugin/screen_model.cpp
    games/pacman/src/pacman/plugin/screen_model.hpp
    games/pacman/src/pacman/plugin/sound_model.cpp
    games/pacman/src/pacman/plugin/sound_model.hpp
    games/pacman/src/pacman/render/painter.cpp
    games/pacman/src/pacman/render/painter.hpp
)
target_include_directories(pacman_plugin PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/games/pacman/src)
target_link_libraries(pacman_plugin PUBLIC nes_editors studio_sdk)
studio_enable_warnings(pacman_plugin)

# Solomon's Key: the room documents, their painter and the editor models.
add_library(solomon_plugin STATIC
    games/solomon/src/solomon/data/documents.cpp
    games/solomon/src/solomon/data/documents.hpp
    games/solomon/src/solomon/data/names.cpp
    games/solomon/src/solomon/data/names.hpp
    games/solomon/src/solomon/data/text.cpp
    games/solomon/src/solomon/data/text.hpp
    games/solomon/src/solomon/plugin/animation_model.cpp
    games/solomon/src/solomon/plugin/animation_model.hpp
    games/solomon/src/solomon/plugin/mirror_models.cpp
    games/solomon/src/solomon/plugin/mirror_models.hpp
    games/solomon/src/solomon/plugin/palette_model.cpp
    games/solomon/src/solomon/plugin/palette_model.hpp
    games/solomon/src/solomon/plugin/presentation_models.cpp
    games/solomon/src/solomon/plugin/presentation_models.hpp
    games/solomon/src/solomon/plugin/room_model.cpp
    games/solomon/src/solomon/plugin/room_model.hpp
    games/solomon/src/solomon/plugin/solomon_plugin.cpp
    games/solomon/src/solomon/plugin/solomon_plugin.hpp
    games/solomon/src/solomon/plugin/solomon_session.cpp
    games/solomon/src/solomon/plugin/solomon_session.hpp
    games/solomon/src/solomon/plugin/sound_model.cpp
    games/solomon/src/solomon/plugin/sound_model.hpp
    games/solomon/src/solomon/plugin/text_models.cpp
    games/solomon/src/solomon/plugin/text_models.hpp
    games/solomon/src/solomon/render/room_painter.cpp
    games/solomon/src/solomon/render/room_painter.hpp
)
target_include_directories(solomon_plugin PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/games/solomon/src)
target_link_libraries(solomon_plugin PUBLIC nes_editors studio_sdk)
studio_enable_warnings(solomon_plugin)

# The one translation unit that names concrete plugins.
add_library(studio_builtin_plugins STATIC
    builtin_plugins.cpp
    builtin_plugins.hpp
)
target_include_directories(studio_builtin_plugins PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(studio_builtin_plugins PUBLIC castle_plugin doraemon_plugin flicky_plugin pacman_plugin smb_plugin solomon_plugin studio_sdk)
studio_enable_warnings(studio_builtin_plugins)

# Tests against the projects the kit prepared in the game repositories
# (games:<repository>/...): they read them only, and pass as skipped where
# a project is not there.
add_executable(flicky_sound_document_test
    games/flicky/tests/sound_document_test.cpp
)
target_link_libraries(flicky_sound_document_test PRIVATE flicky_data studio_sdk)
studio_add_test(flicky_sound_document_test games:flicky_src/.retrolarium/ue/flicky-ue.rlj)

add_executable(castle_music_retune_test
    games/castle/tests/music_retune_test.cpp
)
target_link_libraries(castle_music_retune_test PRIVATE castle_plugin)
studio_add_test(castle_music_retune_test
    games:castle_excellent_src/.retrolarium/castlequest-usa/castle-excellent-castlequest-usa.rlj)

add_executable(solomon_text_and_rooms_test
    games/solomon/tests/text_and_rooms_test.cpp
)
target_link_libraries(solomon_text_and_rooms_test PRIVATE solomon_plugin)
studio_add_test(solomon_text_and_rooms_test games:solomon_no_kagi_src/.retrolarium/usa/solomons-key-usa.rlj)
