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

# Theme and reusable widgets. Knows nothing about games or editors.
add_library(studio_ui STATIC
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
    src/widgets/panel_tabs.cpp
    src/widgets/panel_tabs.hpp
    src/widgets/primary_button.cpp
    src/widgets/primary_button.hpp
    src/widgets/property_grid.cpp
    src/widgets/property_grid.hpp
    src/widgets/rail_button.cpp
    src/widgets/rail_button.hpp
    src/widgets/label.cpp
    src/widgets/label.hpp
    src/widgets/segmented_control.cpp
    src/widgets/segmented_control.hpp
    src/widgets/view_chip.cpp
    src/widgets/view_chip.hpp
    resources/studio.qrc
)
target_include_directories(studio_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(studio_ui PUBLIC Qt5::Core Qt5::Gui Qt5::Widgets Qt5::Svg)
studio_enable_warnings(studio_ui)

add_executable(retrolarium-studio WIN32
    src/app/main.cpp
    src/gallery/theme_gallery.cpp
    src/gallery/theme_gallery.hpp
)
target_link_libraries(retrolarium-studio PRIVATE studio_ui)
studio_enable_warnings(retrolarium-studio)
