/*
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
*/

// C boundary for the skottie (Lottie) runtime.
//
// The shared library hosts Skia's skottie/sksg/skshaper/skresources
// modules compiled against the same skia the app links. It is loaded
// at runtime with QLibrary (core/lottieprovider.cpp), so a missing
// library disables the Lottie layer instead of breaking the app -
// same pattern as the vtracer FFI.
//
// Handles (FRSkottieAnim*) are refcounted by the library: the caller
// owns the reference returned by frsk_load_* and shares it with
// render tasks via frsk_add_ref. All functions are thread-safe;
// concurrent renders on one handle are serialized internally.

#ifndef FRICTIONSKOTTIE_H
#define FRICTIONSKOTTIE_H

#include <cstddef>

class SkImage;

#if defined(_WIN32)
#  if defined(FRICTIONSKOTTIE_BUILD)
#    define FRSK_API __declspec(dllexport)
#  else
#    define FRSK_API
#  endif
#else
#  define FRSK_API __attribute__((visibility("default")))
#endif

struct FRSkottieAnim;

extern "C" {

// Parses a Lottie (bodymovin) JSON from UTF-8 memory. Returns null on
// failure; errOut (optional) receives a short message.
FRSK_API FRSkottieAnim* frsk_load_data(const char* data, size_t len,
                                       char* errOut, size_t errLen);

// Loads a Lottie JSON file. External resources (images) resolve
// relative to baseDir; pass an empty string to use the file's own
// directory.
FRSK_API FRSkottieAnim* frsk_load_file(const char* pathUtf8,
                                       const char* baseDirUtf8,
                                       char* errOut, size_t errLen);

FRSK_API void frsk_add_ref(FRSkottieAnim* anim);
FRSK_API void frsk_release(FRSkottieAnim* anim);

// Animation metrics (0 when the handle is null).
FRSK_API double frsk_duration(const FRSkottieAnim* anim); // seconds
FRSK_API double frsk_fps(const FRSkottieAnim* anim);
FRSK_API int frsk_width(const FRSkottieAnim* anim);
FRSK_API int frsk_height(const FRSkottieAnim* anim);

// Renders the frame at tSec (clamped to [0, duration]) into a new
// raster SkImage (N32 premul, transparent background). The returned
// pointer carries one reference for the caller; wrap it in
// sk_sp<SkImage> to own it. Returns null on failure.
FRSK_API SkImage* frsk_render(FRSkottieAnim* anim, double tSec);

// Human-readable build id for diagnostics.
FRSK_API const char* frsk_version();

} // extern "C"

#endif // FRICTIONSKOTTIE_H
