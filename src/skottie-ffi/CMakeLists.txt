#
# Friction - https://friction.graphics
#
# Copyright (c) Ole-André Rodlie and contributors
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, version 3.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#
# See 'README.md' for more information.
#

# skottie (Lottie) runtime library.
#
# Compiles Skia's skottie/sksg/skshaper/skresources modules (vendored
# in src/skia, disabled in the prebuilt skia.dll) into a standalone
# shared library that links against the same skia the app uses. The
# app loads it at runtime with QLibrary (core/lottieprovider.cpp), so
# a missing library disables the Lottie layer instead of breaking the
# app - same pattern as the vtracer FFI.
#
# The module sources are compiled WITHOUT SKIA_DLL on purpose: SK_API
# expands empty and every Sk* reference resolves through the skia
# import library, while the compiled-in skottie symbols stay private
# to this library (no dllimport/dllexport mismatch with the app-side
# SKIA_DLL build).
#
# Source lists mirror the gn files in the skia tree:
# modules/skottie/skottie.gni, modules/sksg/sksg.gni,
# modules/skshaper/skshaper.gni, modules/skresources/skresources.gni
# (test/tool-only entries excluded; skshaper uses the primitive
# source set - no ICU/HarfBuzz dependency, complex-script shaping is
# not available).

set(SKIA_ROOT ${CMAKE_SOURCE_DIR}/src/skia)

set(FRSK_SKOTTIE_SOURCES
    ${SKIA_ROOT}/modules/skottie/src/Skottie.cpp
    ${SKIA_ROOT}/modules/skottie/src/SkottieJson.cpp
    ${SKIA_ROOT}/modules/skottie/src/SkottieProperty.cpp
    ${SKIA_ROOT}/modules/skottie/src/Transform.cpp
    ${SKIA_ROOT}/modules/skottie/src/Camera.cpp
    ${SKIA_ROOT}/modules/skottie/src/Composition.cpp
    ${SKIA_ROOT}/modules/skottie/src/Layer.cpp
    ${SKIA_ROOT}/modules/skottie/src/Path.cpp
    ${SKIA_ROOT}/modules/skottie/src/animator/Animator.cpp
    ${SKIA_ROOT}/modules/skottie/src/animator/KeyframeAnimator.cpp
    ${SKIA_ROOT}/modules/skottie/src/animator/ScalarKeyframeAnimator.cpp
    ${SKIA_ROOT}/modules/skottie/src/animator/ShapeKeyframeAnimator.cpp
    ${SKIA_ROOT}/modules/skottie/src/animator/TextKeyframeAnimator.cpp
    ${SKIA_ROOT}/modules/skottie/src/animator/Vec2KeyframeAnimator.cpp
    ${SKIA_ROOT}/modules/skottie/src/animator/VectorKeyframeAnimator.cpp
    ${SKIA_ROOT}/modules/skottie/src/effects/CornerPinEffect.cpp
    ${SKIA_ROOT}/modules/skottie/src/effects/DropShadowEffect.cpp
    ${SKIA_ROOT}/modules/skottie/src/effects/Effects.cpp
    ${SKIA_ROOT}/modules/skottie/src/effects/FillEffect.cpp
    ${SKIA_ROOT}/modules/skottie/src/effects/GaussianBlurEffect.cpp
    ${SKIA_ROOT}/modules/skottie/src/effects/GradientEffect.cpp
    ${SKIA_ROOT}/modules/skottie/src/effects/HueSaturationEffect.cpp
    ${SKIA_ROOT}/modules/skottie/src/effects/InvertEffect.cpp
    ${SKIA_ROOT}/modules/skottie/src/effects/LevelsEffect.cpp
    ${SKIA_ROOT}/modules/skottie/src/effects/LinearWipeEffect.cpp
    ${SKIA_ROOT}/modules/skottie/src/effects/MotionBlurEffect.cpp
    ${SKIA_ROOT}/modules/skottie/src/effects/MotionTileEffect.cpp
    ${SKIA_ROOT}/modules/skottie/src/effects/RadialWipeEffect.cpp
    ${SKIA_ROOT}/modules/skottie/src/effects/ShiftChannelsEffect.cpp
    ${SKIA_ROOT}/modules/skottie/src/effects/TintEffect.cpp
    ${SKIA_ROOT}/modules/skottie/src/effects/TransformEffect.cpp
    ${SKIA_ROOT}/modules/skottie/src/effects/TritoneEffect.cpp
    ${SKIA_ROOT}/modules/skottie/src/effects/VenetianBlindsEffect.cpp
    ${SKIA_ROOT}/modules/skottie/src/layers/ImageLayer.cpp
    ${SKIA_ROOT}/modules/skottie/src/layers/NullLayer.cpp
    ${SKIA_ROOT}/modules/skottie/src/layers/PrecompLayer.cpp
    ${SKIA_ROOT}/modules/skottie/src/layers/SolidLayer.cpp
    ${SKIA_ROOT}/modules/skottie/src/layers/TextLayer.cpp
    ${SKIA_ROOT}/modules/skottie/src/layers/shapelayer/Ellipse.cpp
    ${SKIA_ROOT}/modules/skottie/src/layers/shapelayer/FillStroke.cpp
    ${SKIA_ROOT}/modules/skottie/src/layers/shapelayer/Gradient.cpp
    ${SKIA_ROOT}/modules/skottie/src/layers/shapelayer/MergePaths.cpp
    ${SKIA_ROOT}/modules/skottie/src/layers/shapelayer/Polystar.cpp
    ${SKIA_ROOT}/modules/skottie/src/layers/shapelayer/Rectangle.cpp
    ${SKIA_ROOT}/modules/skottie/src/layers/shapelayer/Repeater.cpp
    ${SKIA_ROOT}/modules/skottie/src/layers/shapelayer/RoundCorners.cpp
    ${SKIA_ROOT}/modules/skottie/src/layers/shapelayer/ShapeLayer.cpp
    ${SKIA_ROOT}/modules/skottie/src/layers/shapelayer/TrimPaths.cpp
    ${SKIA_ROOT}/modules/skottie/src/text/RangeSelector.cpp
    ${SKIA_ROOT}/modules/skottie/src/text/SkottieShaper.cpp
    ${SKIA_ROOT}/modules/skottie/src/text/TextAdapter.cpp
    ${SKIA_ROOT}/modules/skottie/src/text/TextAnimator.cpp
    ${SKIA_ROOT}/modules/skottie/src/text/TextValue.cpp
)

set(FRSK_SKSG_SOURCES
    ${SKIA_ROOT}/modules/sksg/src/SkSGClipEffect.cpp
    ${SKIA_ROOT}/modules/sksg/src/SkSGColorFilter.cpp
    ${SKIA_ROOT}/modules/sksg/src/SkSGDashEffect.cpp
    ${SKIA_ROOT}/modules/sksg/src/SkSGDraw.cpp
    ${SKIA_ROOT}/modules/sksg/src/SkSGEffectNode.cpp
    ${SKIA_ROOT}/modules/sksg/src/SkSGGeometryNode.cpp
    ${SKIA_ROOT}/modules/sksg/src/SkSGGeometryTransform.cpp
    ${SKIA_ROOT}/modules/sksg/src/SkSGGradient.cpp
    ${SKIA_ROOT}/modules/sksg/src/SkSGGroup.cpp
    ${SKIA_ROOT}/modules/sksg/src/SkSGImage.cpp
    ${SKIA_ROOT}/modules/sksg/src/SkSGInvalidationController.cpp
    ${SKIA_ROOT}/modules/sksg/src/SkSGMaskEffect.cpp
    ${SKIA_ROOT}/modules/sksg/src/SkSGMerge.cpp
    ${SKIA_ROOT}/modules/sksg/src/SkSGNode.cpp
    ${SKIA_ROOT}/modules/sksg/src/SkSGOpacityEffect.cpp
    ${SKIA_ROOT}/modules/sksg/src/SkSGPaint.cpp
    ${SKIA_ROOT}/modules/sksg/src/SkSGPath.cpp
    ${SKIA_ROOT}/modules/sksg/src/SkSGPlane.cpp
    ${SKIA_ROOT}/modules/sksg/src/SkSGRect.cpp
    ${SKIA_ROOT}/modules/sksg/src/SkSGRenderEffect.cpp
    ${SKIA_ROOT}/modules/sksg/src/SkSGRenderNode.cpp
    ${SKIA_ROOT}/modules/sksg/src/SkSGRoundEffect.cpp
    ${SKIA_ROOT}/modules/sksg/src/SkSGScene.cpp
    ${SKIA_ROOT}/modules/sksg/src/SkSGText.cpp
    ${SKIA_ROOT}/modules/sksg/src/SkSGTransform.cpp
    ${SKIA_ROOT}/modules/sksg/src/SkSGTrimEffect.cpp
)

set(FRSK_SKSHAPER_SOURCES
    ${SKIA_ROOT}/modules/skshaper/src/SkShaper.cpp
    ${SKIA_ROOT}/modules/skshaper/src/SkShaper_primitive.cpp
)

set(FRSK_SKRESOURCES_SOURCES
    ${SKIA_ROOT}/modules/skresources/src/SkResources.cpp
)

# skia internals the modules reference but the prebuilt skia.dll does
# not export: standalone sources compiled here as private copies (the
# exported skia.dll twins remain authoritative for everything else).
# SkTextBlob.cpp is deliberately NOT here: it pulls the whole text
# subsystem; the run iterator skottie needs is reimplemented in
# frsk_skia_stubs.cpp (same for SkOpts::hash_fn and SkAnimCodecPlayer).
set(FRSK_SKIA_EXTRA_SOURCES
    ${SKIA_ROOT}/src/utils/SkJSON.cpp
    ${SKIA_ROOT}/src/utils/SkUTF.cpp
    ${SKIA_ROOT}/src/core/SkM44.cpp
    ${SKIA_ROOT}/src/core/SkArenaAlloc.cpp
    ${SKIA_ROOT}/src/core/SkSemaphore.cpp
    ${SKIA_ROOT}/src/core/SkString.cpp
    ${SKIA_ROOT}/src/core/SkUtils.cpp
    ${SKIA_ROOT}/src/core/SkStringUtils.cpp
    ${SKIA_ROOT}/src/utils/SkOSPath.cpp
    frsk_skia_stubs.cpp
)
if(WIN32)
    list(APPEND FRSK_SKIA_EXTRA_SOURCES
        ${SKIA_ROOT}/src/ports/SkOSFile_win.cpp
        ${SKIA_ROOT}/src/ports/SkOSFile_stdio.cpp)
else()
    list(APPEND FRSK_SKIA_EXTRA_SOURCES
        ${SKIA_ROOT}/src/ports/SkOSFile_posix.cpp
        ${SKIA_ROOT}/src/ports/SkOSFile_stdio.cpp)
endif()

add_library(frictionskottie SHARED
    frictionskottie_impl.cpp
    ${FRSK_SKOTTIE_SOURCES}
    ${FRSK_SKSG_SOURCES}
    ${FRSK_SKSHAPER_SOURCES}
    ${FRSK_SKRESOURCES_SOURCES}
    ${FRSK_SKIA_EXTRA_SOURCES}
)

set_target_properties(frictionskottie PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

target_include_directories(frictionskottie PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${SKIA_ROOT}
    ${SKIA_ROOT}/modules/skottie/include
    ${SKIA_ROOT}/modules/sksg/include
    ${SKIA_ROOT}/modules/skshaper/include
    ${SKIA_ROOT}/modules/skresources/include
)

# see the header comment: NO SKIA_DLL here
target_compile_definitions(frictionskottie PRIVATE
    SK_ENABLE_SKOTTIE
    SK_GAMMA_APPLY_TO_A8
    SKSHAPER_IMPLEMENTATION=1
    FRICTIONSKOTTIE_BUILD
)

if(WIN32)
    set(FRSK_SKIA_LIBRARIES skia user32 opengl32)
    set(FRSK_SKIA_LIBRARIES_DIRS ${CMAKE_SOURCE_DIR}/sdk/bin)
elseif(APPLE)
    set(FRSK_SKIA_LIBRARIES skia z
        "-framework CoreFoundation" "-framework CoreGraphics"
        "-framework CoreText" "-framework CoreServices")
    if(BUILD_SKIA)
        set(FRSK_SKIA_LIBRARIES_DIRS ${CMAKE_BINARY_DIR}/src/skia)
    else()
        set(FRSK_SKIA_LIBRARIES_DIRS ${SKIA_LIB_PATH})
    endif()
else()
    # mirror friction-common: static builds produce libskia.a,
    # shared builds produce libskia-friction.so
    if(SKIA_STATIC)
        set(FRSK_SKIA_LIBRARIES skia)
    else()
        set(FRSK_SKIA_LIBRARIES skia-friction)
    endif()
    if(BUILD_SKIA)
        set(FRSK_SKIA_LIBRARIES_DIRS ${CMAKE_BINARY_DIR}/src/skia)
    else()
        set(FRSK_SKIA_LIBRARIES_DIRS ${SKIA_LIB_PATH})
    endif()
endif()

# the skia lib is produced by the `skialib` custom target (gn/ninja);
# without this dependency ninja may link frictionskottie before it exists
if(BUILD_SKIA)
    add_dependencies(frictionskottie skialib)
endif()

target_link_directories(frictionskottie PRIVATE ${FRSK_SKIA_LIBRARIES_DIRS})
target_link_libraries(frictionskottie PRIVATE ${FRSK_SKIA_LIBRARIES})

if(MSVC)
    # see msvc_compat.h
    target_compile_options(frictionskottie PRIVATE
        "/FI${CMAKE_CURRENT_SOURCE_DIR}/msvc_compat.h"
        /wd4244 /wd4996 /wd5030)
endif()

if(WIN32)
    # keep a copy next to the app executable for dev runs; release
    # packaging picks the library up from the build output
    add_custom_command(TARGET frictionskottie POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:frictionskottie>
            ${CMAKE_BINARY_DIR}/src/app/$<CONFIG>/
    )
endif()
