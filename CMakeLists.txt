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
find_package(Qt6 6.7 REQUIRED COMPONENTS Core Gui Widgets ShaderTools Concurrent
                       OPTIONAL_COMPONENTS GuiPrivate)

qt_standard_project_setup()

# libvips is the image pipeline. It ships pkg-config files; OpenCV/LibRaw/Lensfun
# are intentionally NOT wired up yet — they arrive with later milestones.
find_package(PkgConfig REQUIRED)
pkg_check_modules(VIPS REQUIRED IMPORTED_TARGET vips-cpp)

if(MSVC)
    set(LUMEN_WARNINGS /W3)
else()
    set(LUMEN_WARNINGS -Wall -Wextra)
endif()

# --- Core library ---------------------------------------------------------
# UI-free image pipeline + edit graph, so it can be unit-tested without pulling
# in Qt Widgets / RHI. Qt6::Gui is here only for QImage in the loader.

add_library(lumen_core STATIC
    src/core/PreviewState.h
    src/core/Image.h
    src/core/Image.cpp
    src/core/ImageBuffer.h
    src/core/ImageBuffer.cpp
    src/core/EditNode.h
    src/core/EditNode.cpp
    src/core/EditGraph.h
    src/core/EditGraph.cpp
    src/core/Layer.h
    src/core/Layer.cpp
    src/core/TuneNode.h
    src/core/TuneNode.cpp
    src/core/Curve.h
    src/core/Curve.cpp
    src/core/CurvesNode.h
    src/core/CurvesNode.cpp
    src/core/Lut3D.h
    src/core/Lut3D.cpp
    src/core/LutNode.h
    src/core/LutNode.cpp
    src/core/MonoNode.h
    src/core/MonoNode.cpp
    src/core/NodeFactory.h
    src/core/NodeFactory.cpp
    src/core/Project.h
    src/core/Project.cpp
    src/core/GuidedFilter.h
    src/core/GuidedFilter.cpp
    src/core/SelectiveMask.h
    src/core/SelectiveMask.cpp
    src/core/MaskSpec.h
    src/core/MaskSpec.cpp
    src/core/Inpaint.h
    src/core/Inpaint.cpp
    src/core/HealNode.h
    src/core/HealNode.cpp
)
target_include_directories(lumen_core PUBLIC src)
target_link_libraries(lumen_core PUBLIC Qt6::Core Qt6::Gui PkgConfig::VIPS)
target_compile_options(lumen_core PRIVATE ${LUMEN_WARNINGS})

# --- Application ----------------------------------------------------------

qt_add_executable(lumen
    src/main.cpp
    src/gpu/CanvasWidget.h
    src/gpu/CanvasWidget.cpp
    src/gpu/ZoomMath.h
    src/input/InputController.h
    src/input/InputController.cpp
    src/input/CommandPalette.h
    src/input/CommandPalette.cpp
    src/ui/TonePanel.h
    src/ui/TonePanel.cpp
    src/ui/CurvesPanel.h
    src/ui/CurvesPanel.cpp
    src/ui/LooksPanel.h
    src/ui/LooksPanel.cpp
    src/ui/HealPanel.h
    src/ui/HealPanel.cpp
    src/ui/LayersPanel.h
    src/ui/LayersPanel.cpp
    src/ui/MonoPanel.h
    src/ui/MonoPanel.cpp
    src/ui/MaskGizmo.h
    src/ui/MaskGizmo.cpp
    src/ui/ExportDialog.h
    src/ui/ExportDialog.cpp
    src/ui/MainWindow.h
    src/ui/MainWindow.cpp
)

target_link_libraries(lumen PRIVATE
    lumen_core
    Qt6::Widgets
    Qt6::Concurrent
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
        src/gpu/shaders/present.vert
        src/gpu/shaders/present.frag
)

if(APPLE)
    set_target_properties(lumen PROPERTIES MACOSX_BUNDLE ON)
endif()

# Treat warnings seriously in our own code (not in deps).
target_compile_options(lumen PRIVATE ${LUMEN_WARNINGS})

# --- Tests ----------------------------------------------------------------

include(CTest)
if(BUILD_TESTING)
    add_executable(edit_graph_test tests/edit_graph_test.cpp)
    target_link_libraries(edit_graph_test PRIVATE lumen_core)
    target_compile_options(edit_graph_test PRIVATE ${LUMEN_WARNINGS})
    add_test(NAME edit_graph_test COMMAND edit_graph_test)

    add_executable(tune_node_test tests/tune_node_test.cpp)
    target_link_libraries(tune_node_test PRIVATE lumen_core)
    target_compile_options(tune_node_test PRIVATE ${LUMEN_WARNINGS})
    add_test(NAME tune_node_test COMMAND tune_node_test)

    add_executable(export_test tests/export_test.cpp)
    target_link_libraries(export_test PRIVATE lumen_core)
    target_compile_options(export_test PRIVATE ${LUMEN_WARNINGS})
    add_test(NAME export_test COMMAND export_test)

    add_executable(undo_test tests/undo_test.cpp)
    target_link_libraries(undo_test PRIVATE lumen_core)
    target_compile_options(undo_test PRIVATE ${LUMEN_WARNINGS})
    add_test(NAME undo_test COMMAND undo_test)

    # ZoomMath is a header in src/gpu; lumen_core exposes the src include dir.
    add_executable(zoom_test tests/zoom_test.cpp)
    target_link_libraries(zoom_test PRIVATE lumen_core)
    target_compile_options(zoom_test PRIVATE ${LUMEN_WARNINGS})
    add_test(NAME zoom_test COMMAND zoom_test)

    add_executable(curves_test tests/curves_test.cpp)
    target_link_libraries(curves_test PRIVATE lumen_core)
    target_compile_options(curves_test PRIVATE ${LUMEN_WARNINGS})
    add_test(NAME curves_test COMMAND curves_test)

    add_executable(lut3d_test tests/lut3d_test.cpp)
    target_link_libraries(lut3d_test PRIVATE lumen_core)
    target_compile_options(lut3d_test PRIVATE ${LUMEN_WARNINGS})
    add_test(NAME lut3d_test COMMAND lut3d_test)

    add_executable(lutnode_test tests/lutnode_test.cpp)
    target_link_libraries(lutnode_test PRIVATE lumen_core)
    target_compile_options(lutnode_test PRIVATE ${LUMEN_WARNINGS})
    add_test(NAME lutnode_test COMMAND lutnode_test)


    add_executable(guided_filter_test tests/guided_filter_test.cpp)
    target_link_libraries(guided_filter_test PRIVATE lumen_core)
    target_compile_options(guided_filter_test PRIVATE ${LUMEN_WARNINGS})
    add_test(NAME guided_filter_test COMMAND guided_filter_test)

    add_executable(mask_test tests/mask_test.cpp)
    target_link_libraries(mask_test PRIVATE lumen_core)
    target_compile_options(mask_test PRIVATE ${LUMEN_WARNINGS})
    add_test(NAME mask_test COMMAND mask_test)

    add_executable(layers_test tests/layers_test.cpp)
    target_link_libraries(layers_test PRIVATE lumen_core)
    target_compile_options(layers_test PRIVATE ${LUMEN_WARNINGS})
    add_test(NAME layers_test COMMAND layers_test)

    add_executable(mono_test tests/mono_test.cpp)
    target_link_libraries(mono_test PRIVATE lumen_core)
    target_compile_options(mono_test PRIVATE ${LUMEN_WARNINGS})
    add_test(NAME mono_test COMMAND mono_test)

    add_executable(layer_undo_test tests/layer_undo_test.cpp)
    target_link_libraries(layer_undo_test PRIVATE lumen_core)
    target_compile_options(layer_undo_test PRIVATE ${LUMEN_WARNINGS})
    add_test(NAME layer_undo_test COMMAND layer_undo_test)

    add_executable(project_test tests/project_test.cpp)
    target_link_libraries(project_test PRIVATE lumen_core)
    target_compile_options(project_test PRIVATE ${LUMEN_WARNINGS})
    add_test(NAME project_test COMMAND project_test)

    add_executable(heal_test tests/heal_test.cpp)
    target_link_libraries(heal_test PRIVATE lumen_core)
    target_compile_options(heal_test PRIVATE ${LUMEN_WARNINGS})
    add_test(NAME heal_test COMMAND heal_test)
endif()
