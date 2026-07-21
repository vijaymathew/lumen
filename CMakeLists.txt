cmake_minimum_required(VERSION 3.24)

project(lumen
    VERSION 0.1.1
    DESCRIPTION "An immersive, command-palette-driven photo editor"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Record the directories of any non-system libraries the app links (e.g. an
# imaging lib from a custom prefix) in the installed binary's RPATH, so it
# resolves them at runtime and not only from the build tree. Qt is handled
# separately below — qt_standard_project_setup() pins its own install RPATH,
# which this heuristic doesn't extend. (Harmless for system libs: implicit
# /usr/lib dirs are never added.)
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH ON)

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

# libvips is the image pipeline. LibRaw decodes camera RAW. OpenCV is not wired
# up yet — it arrives with a later milestone.
find_package(PkgConfig REQUIRED)
pkg_check_modules(VIPS REQUIRED IMPORTED_TARGET vips-cpp)
pkg_check_modules(LIBRAW REQUIRED IMPORTED_TARGET libraw)

# Lensfun (lens-correction database + geometry models) is OPTIONAL: when present
# the LensCorrectionNode does automatic distortion/TCA/vignetting from EXIF; when
# absent the node still offers manual perspective. Keeping it optional avoids the
# hard-dependency CI breakage a REQUIRED check would cause on bare runners.
pkg_check_modules(LENSFUN IMPORTED_TARGET lensfun)
if(LENSFUN_FOUND)
    message(STATUS "Lensfun ${LENSFUN_VERSION} found — automatic lens correction enabled")
else()
    message(STATUS "Lensfun not found — lens correction is perspective-only")
endif()

# Little CMS (lcms2) drives export colour management — it synthesises the ICC
# profiles the wide-gamut export paths transform into. It ships as a libvips
# dependency, but the dev headers may be absent on bare CI runners, so it is
# OPTIONAL: without it, export is limited to sRGB (see Image::ColorSpace).
pkg_check_modules(LCMS2 IMPORTED_TARGET lcms2)
if(LCMS2_FOUND)
    message(STATUS "lcms2 ${LCMS2_VERSION} found — export colour management enabled")
else()
    message(STATUS "lcms2 not found — export limited to sRGB")
endif()

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
    src/core/WhiteBalance.h
    src/core/WhiteBalance.cpp
    src/core/ColorGradeNode.h
    src/core/ColorGradeNode.cpp
    src/core/ColorMixerNode.h
    src/core/ColorMixerNode.cpp
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
    src/core/GrainNode.h
    src/core/GrainNode.cpp
    src/core/CropState.h
    src/core/CropState.cpp
    src/core/Vignette.h
    src/core/Vignette.cpp
    src/core/NodeFactory.h
    src/core/NodeFactory.cpp
    src/core/Project.h
    src/core/Project.cpp
    src/core/Document.h
    src/core/Document.cpp
    src/core/Preset.h
    src/core/Preset.cpp
    src/core/BuiltinPresets.h
    src/core/BuiltinPresets.cpp
    src/core/Autosave.h
    src/core/Autosave.cpp
    src/core/RawLoader.h
    src/core/RawLoader.cpp
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
    src/core/LensCorrectionNode.h
    src/core/LensCorrectionNode.cpp
    src/core/SharpenNode.h
    src/core/SharpenNode.cpp
    src/core/StructureNode.h
    src/core/StructureNode.cpp
    src/core/DenoiseNode.h
    src/core/DenoiseNode.cpp
    src/core/DefringeNode.h
    src/core/DefringeNode.cpp
    src/core/Histogram.h
    src/core/Histogram.cpp
)
target_include_directories(lumen_core PUBLIC src)
target_link_libraries(lumen_core PUBLIC Qt6::Core Qt6::Gui PkgConfig::VIPS PkgConfig::LIBRAW)
if(LENSFUN_FOUND)
    target_link_libraries(lumen_core PUBLIC PkgConfig::LENSFUN)
    target_compile_definitions(lumen_core PUBLIC LUMEN_HAVE_LENSFUN)
endif()
if(LCMS2_FOUND)
    target_link_libraries(lumen_core PUBLIC PkgConfig::LCMS2)
    target_compile_definitions(lumen_core PUBLIC LUMEN_HAVE_LCMS)
endif()
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
    src/ui/ColorWheel.cpp
    src/ui/ColorGradePanel.cpp
    src/ui/ColorMixerPanel.h
    src/ui/ColorMixerPanel.cpp
    src/ui/CurvesPanel.h
    src/ui/CurvesPanel.cpp
    src/ui/LooksPanel.h
    src/ui/LooksPanel.cpp
    src/ui/PresetsPanel.h
    src/ui/PresetsPanel.cpp
    src/ui/HealPanel.h
    src/ui/HealPanel.cpp
    src/ui/LayersPanel.h
    src/ui/LayersPanel.cpp
    src/ui/AdjustmentsPanel.h
    src/ui/AdjustmentsPanel.cpp
    src/ui/InfoPanel.h
    src/ui/InfoPanel.cpp
    src/ui/MonoPanel.h
    src/ui/MonoPanel.cpp
    src/ui/GrainPanel.h
    src/ui/GrainPanel.cpp
    src/ui/CropPanel.h
    src/ui/CropPanel.cpp
    src/ui/VignettePanel.h
    src/ui/VignettePanel.cpp
    src/ui/CropGizmo.h
    src/ui/CropGizmo.cpp
    src/ui/LensPanel.h
    src/ui/LensPanel.cpp
    src/ui/SharpenPanel.h
    src/ui/SharpenPanel.cpp
    src/ui/StructurePanel.h
    src/ui/StructurePanel.cpp
    src/ui/DenoisePanel.h
    src/ui/DenoisePanel.cpp
    src/ui/DefringePanel.h
    src/ui/DefringePanel.cpp
    src/ui/RawSettingsPanel.h
    src/ui/RawSettingsPanel.cpp
    src/ui/HistogramWidget.h
    src/ui/HistogramWidget.cpp
    src/ui/MaskGizmo.h
    src/ui/MaskGizmo.cpp
    src/ui/ZoneGizmo.h
    src/ui/ZoneGizmo.cpp
    src/ui/ExportDialog.h
    src/ui/ExportDialog.cpp
    src/ui/ImageOpenDialog.h
    src/ui/ImageOpenDialog.cpp
    src/ui/ThumbnailProxyModel.h
    src/ui/ThumbnailProxyModel.cpp
    src/ui/AppIcon.h
    src/ui/AppIcon.cpp
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

# Single source of truth for the app version: the project() version above, handed
# to the code as a string so main.cpp and the window title never drift from it.
target_compile_definitions(lumen PRIVATE LUMEN_VERSION="${PROJECT_VERSION}")

# --- Install --------------------------------------------------------------
# `cmake --install build` (or install.sh) drops the binary into the prefix:
# a plain executable under bin/ on Linux, the lumen.app bundle at the prefix
# root on macOS.
include(GNUInstallDirs)
install(TARGETS lumen
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# The app may link a Qt from a non-system prefix (e.g. one install.sh fetched
# into .qt/). Add that Qt's lib dir to the installed binary's RPATH so it
# resolves libQt6*.so at runtime — otherwise the installed lumen only runs from
# the build tree. Derived from Qt6_DIR (<prefix>/lib/cmake/Qt6). On macOS the
# .app bundle carries its frameworks, so this is Linux/BSD only. Harmless for a
# system Qt (that lib dir is on the default search path already).
if(UNIX AND NOT APPLE)
    get_filename_component(_qt_lib_dir "${Qt6_DIR}/../.." ABSOLUTE)
    # Set the install RPATH explicitly: the $ORIGIN-relative defaults (for any
    # co-installed libs) plus the Qt lib dir. $ORIGIN is a literal here — the
    # dynamic loader expands it at runtime, not CMake.
    set_property(TARGET lumen PROPERTY INSTALL_RPATH
        "$ORIGIN;$ORIGIN/../lib;${_qt_lib_dir}")
endif()

# Desktop integration on Linux/BSD (macOS uses the .app bundle instead): a
# .desktop entry and a scalable icon, so `make install` leaves a launchable,
# icon'd app.
if(UNIX AND NOT APPLE)
    install(FILES packaging/lumen.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES packaging/lumen.svg
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
        RENAME lumen.svg)
endif()

# --- Tests ----------------------------------------------------------------

include(CTest)
if(BUILD_TESTING)
    add_executable(edit_graph_test tests/edit_graph_test.cpp)
    target_link_libraries(edit_graph_test PRIVATE lumen_core)
    target_compile_options(edit_graph_test PRIVATE ${LUMEN_WARNINGS})
    add_test(NAME edit_graph_test COMMAND edit_graph_test)

    add_executable(builtin_presets_test tests/builtin_presets_test.cpp)
    target_link_libraries(builtin_presets_test PRIVATE lumen_core)
    target_compile_options(builtin_presets_test PRIVATE ${LUMEN_WARNINGS})
    add_test(NAME builtin_presets_test COMMAND builtin_presets_test)

    # GUI smoke check for the Presets browser — renders the real panel to a PNG.
    # Needs a display, so it's a buildable target only (not a CTest assertion).
    qt_add_executable(presets_gui_smoke
        tests/presets_gui_smoke.cpp
        src/ui/PresetsPanel.h
        src/ui/PresetsPanel.cpp)
    target_link_libraries(presets_gui_smoke PRIVATE lumen_core Qt6::Widgets)

    add_executable(tune_node_test tests/tune_node_test.cpp)
    target_link_libraries(tune_node_test PRIVATE lumen_core)
    target_compile_options(tune_node_test PRIVATE ${LUMEN_WARNINGS})
    add_test(NAME tune_node_test COMMAND tune_node_test)

    add_executable(whitebalance_test tests/whitebalance_test.cpp)
    target_link_libraries(whitebalance_test PRIVATE lumen_core)
    target_compile_options(whitebalance_test PRIVATE ${LUMEN_WARNINGS})
    add_test(NAME whitebalance_test COMMAND whitebalance_test)

    add_executable(colorgrade_test tests/colorgrade_test.cpp)
    target_link_libraries(colorgrade_test PRIVATE lumen_core)
    target_compile_options(colorgrade_test PRIVATE ${LUMEN_WARNINGS})
    add_test(NAME colorgrade_test COMMAND colorgrade_test)

    add_executable(colormixer_test tests/colormixer_test.cpp)
    target_link_libraries(colormixer_test PRIVATE lumen_core)
    target_compile_options(colormixer_test PRIVATE ${LUMEN_WARNINGS})
    add_test(NAME colormixer_test COMMAND colormixer_test)

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

    add_executable(preset_test tests/preset_test.cpp)
    target_link_libraries(preset_test PRIVATE lumen_core)
    target_compile_options(preset_test PRIVATE ${LUMEN_WARNINGS})
    add_test(NAME preset_test COMMAND preset_test)

    add_executable(raw_test tests/raw_test.cpp)
    target_link_libraries(raw_test PRIVATE lumen_core)
    target_compile_options(raw_test PRIVATE ${LUMEN_WARNINGS})
    add_test(NAME raw_test COMMAND raw_test)

    add_executable(heal_test tests/heal_test.cpp)
    target_link_libraries(heal_test PRIVATE lumen_core)
    target_compile_options(heal_test PRIVATE ${LUMEN_WARNINGS})
    add_test(NAME heal_test COMMAND heal_test)

    add_executable(lens_node_test tests/lens_node_test.cpp)
    target_link_libraries(lens_node_test PRIVATE lumen_core)
    target_compile_options(lens_node_test PRIVATE ${LUMEN_WARNINGS})
    add_test(NAME lens_node_test COMMAND lens_node_test)

    add_executable(crop_test tests/crop_test.cpp)
    target_link_libraries(crop_test PRIVATE lumen_core)
    target_compile_options(crop_test PRIVATE ${LUMEN_WARNINGS})
    add_test(NAME crop_test COMMAND crop_test)

    add_executable(vignette_test tests/vignette_test.cpp)
    target_link_libraries(vignette_test PRIVATE lumen_core)
    target_compile_options(vignette_test PRIVATE ${LUMEN_WARNINGS})
    add_test(NAME vignette_test COMMAND vignette_test)

    add_executable(defringe_test tests/defringe_test.cpp)
    target_link_libraries(defringe_test PRIVATE lumen_core)
    target_compile_options(defringe_test PRIVATE ${LUMEN_WARNINGS})
    add_test(NAME defringe_test COMMAND defringe_test)

    add_executable(rawoptions_test tests/rawoptions_test.cpp)
    target_link_libraries(rawoptions_test PRIVATE lumen_core)
    target_compile_options(rawoptions_test PRIVATE ${LUMEN_WARNINGS})
    add_test(NAME rawoptions_test COMMAND rawoptions_test)

    add_executable(autosave_test tests/autosave_test.cpp)
    target_link_libraries(autosave_test PRIVATE lumen_core)
    target_compile_options(autosave_test PRIVATE ${LUMEN_WARNINGS})
    add_test(NAME autosave_test COMMAND autosave_test)

    add_executable(sharpen_test tests/sharpen_test.cpp)
    target_link_libraries(sharpen_test PRIVATE lumen_core)
    target_compile_options(sharpen_test PRIVATE ${LUMEN_WARNINGS})
    add_test(NAME sharpen_test COMMAND sharpen_test)

    add_executable(structure_test tests/structure_test.cpp)
    target_link_libraries(structure_test PRIVATE lumen_core)
    target_compile_options(structure_test PRIVATE ${LUMEN_WARNINGS})
    add_test(NAME structure_test COMMAND structure_test)

    add_executable(histogram_test tests/histogram_test.cpp)
    target_link_libraries(histogram_test PRIVATE lumen_core)
    target_compile_options(histogram_test PRIVATE ${LUMEN_WARNINGS})
    add_test(NAME histogram_test COMMAND histogram_test)

    add_executable(denoise_test tests/denoise_test.cpp)
    target_link_libraries(denoise_test PRIVATE lumen_core)
    target_compile_options(denoise_test PRIVATE ${LUMEN_WARNINGS})
    add_test(NAME denoise_test COMMAND denoise_test)

    add_executable(grain_test tests/grain_test.cpp)
    target_link_libraries(grain_test PRIVATE lumen_core)
    target_compile_options(grain_test PRIVATE ${LUMEN_WARNINGS})
    add_test(NAME grain_test COMMAND grain_test)
endif()
