cmake_minimum_required(VERSION 3.21)

project(
    RetrolariumStudio
    VERSION 0.1.0
    DESCRIPTION "Configurable resource studio for retro games"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# The Longreach Qt Windows SDK ships static Qt and a static C++ runtime.
# CMake does not inherit the final -static flag from the Qt installation.
if(MINGW)
    add_link_options(-static $<$<CONFIG:Release>:-s>)
endif()

find_package(Qt5 5.15 REQUIRED COMPONENTS Core Gui Widgets Svg)

function(studio_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

# Module layering. Each library may only link the ones above it:
#
#   studio_ui       theme, widgets, window frame       (no studio knowledge)
#   studio_sdk      plugin contracts                   (no UI, no games)
#   studio_runtime  processes, tool lookup, emulator    sdk
#   studio_editors  universal editors                  ui + sdk + runtime
#   studio_gallery  theme test bench                   ui
#   studio_shell    main window                        ui + sdk + editors + gallery
#   plugins/*       platform and game code             sdk only
#   app             entry point, names the plugins     everything

add_library(studio_ui STATIC
    src/frame/native_frame.cpp
    src/frame/native_frame.hpp
    src/frame/studio_dialog.cpp
    src/frame/studio_dialog.hpp
    src/frame/studio_window.cpp
    src/frame/studio_window.hpp
    src/frame/title_bar.cpp
    src/frame/title_bar.hpp
    src/theme/icons.cpp
    src/theme/icons.hpp
    src/theme/industry_style.cpp
    src/theme/industry_style.hpp
    src/theme/paint.cpp
    src/theme/paint.hpp
    src/theme/stylesheet.cpp
    src/theme/stylesheet.hpp
    src/theme/theme.cpp
    src/theme/theme.hpp
    src/theme/tokens.cpp
    src/theme/tokens.hpp
    src/theme/typography.cpp
    src/theme/typography.hpp
    src/theme/window_frame.cpp
    src/theme/window_frame.hpp
    src/widgets/badge.cpp
    src/widgets/badge.hpp
    src/widgets/budget_bar.cpp
    src/widgets/budget_bar.hpp
    src/widgets/collapsible_section.cpp
    src/widgets/collapsible_section.hpp
    src/widgets/document_list.cpp
    src/widgets/document_list.hpp
    src/widgets/label.cpp
    src/widgets/label.hpp
    src/widgets/panel_tabs.cpp
    src/widgets/panel_tabs.hpp
    src/widgets/primary_button.cpp
    src/widgets/primary_button.hpp
    src/widgets/property_grid.cpp
    src/widgets/property_grid.hpp
    src/widgets/rail_button.cpp
    src/widgets/rail_button.hpp
    src/widgets/segmented_control.cpp
    src/widgets/segmented_control.hpp
    src/widgets/view_chip.cpp
    src/widgets/view_chip.hpp
    resources/studio.qrc
)
target_include_directories(studio_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(studio_ui PUBLIC Qt5::Core Qt5::Gui Qt5::Widgets Qt5::Svg)
studio_enable_warnings(studio_ui)

add_library(studio_sdk STATIC
    src/sdk/editor_model.cpp
    src/sdk/editor_model.hpp
    src/sdk/plugin.cpp
    src/sdk/plugin.hpp
    src/sdk/project_file.cpp
    src/sdk/project_file.hpp
    src/sdk/session.cpp
    src/sdk/session.hpp
    src/sdk/types.hpp
)
# QUndoStack lives in QtWidgets in Qt 5; the SDK uses no widgets otherwise.
target_include_directories(studio_sdk PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(studio_sdk PUBLIC Qt5::Core Qt5::Gui Qt5::Widgets)
studio_enable_warnings(studio_sdk)

add_library(studio_runtime STATIC
    src/runtime/audio_player.cpp
    src/runtime/audio_player.hpp
    src/runtime/emulator_runner.cpp
    src/runtime/emulator_runner.hpp
    src/runtime/process_runner.cpp
    src/runtime/process_runner.hpp
    src/runtime/tool_locator.cpp
    src/runtime/tool_locator.hpp
)
target_link_libraries(studio_runtime PUBLIC studio_sdk Qt5::Core Qt5::Gui Qt5::Widgets)
if(WIN32)
    target_link_libraries(studio_runtime PRIVATE winmm)
endif()
studio_enable_warnings(studio_runtime)

add_library(studio_editors STATIC
    src/editors/composition_editor.cpp
    src/editors/composition_editor.hpp
    src/editors/editor_view.cpp
    src/editors/editor_view.hpp
    src/editors/emulator_view.cpp
    src/editors/emulator_view.hpp
    src/editors/mime.hpp
    src/editors/palette_editor.cpp
    src/editors/palette_editor.hpp
    src/editors/sequencer_editor.cpp
    src/editors/sequencer_editor.hpp
    src/editors/table_editor.cpp
    src/editors/table_editor.hpp
    src/editors/tile_editor.cpp
    src/editors/tile_editor.hpp
    src/editors/tilemap_editor.cpp
    src/editors/tilemap_editor.hpp
)
target_link_libraries(studio_editors PUBLIC studio_ui studio_sdk studio_runtime)
studio_enable_warnings(studio_editors)

add_library(studio_gallery STATIC
    src/gallery/theme_gallery.cpp
    src/gallery/theme_gallery.hpp
)
target_link_libraries(studio_gallery PUBLIC studio_ui)
studio_enable_warnings(studio_gallery)

add_library(studio_shell STATIC
    src/shell/bottom_panel.cpp
    src/shell/bottom_panel.hpp
    src/shell/inspector_panel.cpp
    src/shell/inspector_panel.hpp
    src/shell/main_window.cpp
    src/shell/main_window.hpp
    src/shell/side_panels.cpp
    src/shell/side_panels.hpp
)
target_link_libraries(studio_shell PUBLIC studio_editors studio_gallery studio_runtime studio_sdk studio_ui)
studio_enable_warnings(studio_shell)

# The shared, Qt-free core (codecs with golden tests, also shipped with the
# kit as rlcore). Platform plugins wrap what they need from it.
set(RETROLARIUM_CORE_BUILD_CLI OFF CACHE BOOL "" FORCE)
set(RETROLARIUM_CORE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../retrolarium_core retrolarium_core)

add_subdirectory(plugins)

add_executable(retrolarium-studio WIN32
    src/app/main.cpp
)
# The one place the version is set: project(VERSION) above.
target_compile_definitions(retrolarium-studio PRIVATE RETROLARIUM_STUDIO_VERSION="${PROJECT_VERSION}")
if(WIN32)
    enable_language(RC)
    configure_file(resources/version.rc.in ${CMAKE_CURRENT_BINARY_DIR}/version.rc @ONLY)
    target_sources(retrolarium-studio PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/version.rc)
endif()
target_link_libraries(retrolarium-studio PRIVATE studio_shell studio_builtin_plugins)
studio_enable_warnings(retrolarium-studio)
