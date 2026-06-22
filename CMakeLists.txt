cmake_minimum_required(VERSION 3.24)

project(lumen
    VERSION 0.1.0
    DESCRIPTION "An immersive, command-palette-driven photo editor"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

# --- Dependencies ---------------------------------------------------------

# Qt RHI (QRhiWidget) requires Qt 6.7+. macOS is a v1 target and OpenGL is
# deprecated there, so we render through RHI (Metal/Vulkan/OpenGL) rather than
# raw GL. See docs/DESIGN.md §7.
#
# The QRhi API (rhi/qrhi.h) ships as a private header. Normally that means
# linking Qt6::GuiPrivate, but some binary Qt builds (e.g. aqtinstall) ship the
# private headers without the GuiPrivate CMake package — so it's optional here
# and we fall back to adding the include paths by hand below.
find_package(Qt6 6.7 REQUIRED COMPONENTS Core Gui Widgets ShaderTools
                       OPTIONAL_COMPONENTS GuiPrivate)

qt_standard_project_setup()

# libvips is the image pipeline. It ships pkg-config files; OpenCV/LibRaw/Lensfun
# are intentionally NOT wired up yet — they arrive with later milestones.
find_package(PkgConfig REQUIRED)
pkg_check_modules(VIPS REQUIRED IMPORTED_TARGET vips-cpp)

# --- Application ----------------------------------------------------------

qt_add_executable(lumen
    src/main.cpp
    src/core/ImageBuffer.h
    src/core/ImageBuffer.cpp
    src/gpu/CanvasWidget.h
    src/gpu/CanvasWidget.cpp
    src/input/InputController.h
    src/input/InputController.cpp
    src/input/CommandPalette.h
    src/input/CommandPalette.cpp
    src/ui/MainWindow.h
    src/ui/MainWindow.cpp
)

target_include_directories(lumen PRIVATE src)

target_link_libraries(lumen PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    PkgConfig::VIPS
)

# Access to the private QRhi headers (rhi/qrhi.h).
if(TARGET Qt6::GuiPrivate)
    target_link_libraries(lumen PRIVATE Qt6::GuiPrivate)
else()
    # Fallback for Qt builds that ship private headers without the GuiPrivate
    # CMake package. Add the versioned private include dirs manually.
    get_filename_component(_qt_prefix "${Qt6Gui_DIR}/../../.." ABSOLUTE)
    target_include_directories(lumen PRIVATE
        "${_qt_prefix}/include/QtCore/${Qt6Gui_VERSION}"
        "${_qt_prefix}/include/QtCore/${Qt6Gui_VERSION}/QtCore"
        "${_qt_prefix}/include/QtGui/${Qt6Gui_VERSION}"
        "${_qt_prefix}/include/QtGui/${Qt6Gui_VERSION}/QtGui"
    )
endif()

# Compile the preview shaders to .qsb and embed them in the binary's resources.
qt_add_shaders(lumen "lumen_shaders"
    PREFIX "/shaders"
    BASE "src/gpu/shaders"
    FILES
        src/gpu/shaders/texture.vert
        src/gpu/shaders/texture.frag
)

if(APPLE)
    set_target_properties(lumen PROPERTIES MACOSX_BUNDLE ON)
endif()

# Treat warnings seriously in our own code (not in deps).
if(MSVC)
    target_compile_options(lumen PRIVATE /W3)
else()
    target_compile_options(lumen PRIVATE -Wall -Wextra)
endif()
