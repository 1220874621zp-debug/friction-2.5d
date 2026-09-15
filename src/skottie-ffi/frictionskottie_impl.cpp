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

// skottie (Lottie) runtime wrapper. See frictionskottie.h for the
// contract. skottie animations are not thread-safe per instance:
// frsk_render serializes access with a per-handle mutex (render
// tasks from the app's CPU thread pool may run concurrently).

#define FRICTIONSKOTTIE_BUILD 1

#include "frictionskottie.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkImage.h"
#include "include/core/SkSurface.h"
#include "modules/skottie/include/Skottie.h"
#include "SkResources.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

struct FRSkottieAnim {
    sk_sp<skottie::Animation> fAnim;
    std::mutex fMutex;
    int fRefs = 1;
};

namespace {

void setErr(char* const errOut, const size_t errLen,
            const std::string& msg)
{
    if (errOut && errLen > 0) {
        std::snprintf(errOut, errLen, "%s", msg.c_str());
    }
}

std::string readFileBytes(const char* const path)
{
    std::FILE* const f = std::fopen(path, "rb");
    if (!f) { return {}; }
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    if (size <= 0 || size > 512L * 1024 * 1024) {
        std::fclose(f);
        return {};
    }
    std::fseek(f, 0, SEEK_SET);
    std::string bytes(static_cast<size_t>(size), '\0');
    const size_t read = std::fread(&bytes[0], 1, bytes.size(), f);
    std::fclose(f);
    if (read != bytes.size()) { return {}; }
    return bytes;
}

FRSkottieAnim* buildAnim(skottie::Animation::Builder& builder,
                         const char* const data, const size_t len,
                         char* const errOut, const size_t errLen)
{
    auto anim = builder.make(data, len);
    if (!anim) { setErr(errOut, errLen, "skottie: failed to parse Lottie JSON");
        return nullptr; }
    const auto& size = anim->size();
    if (size.width() <= 0.f || size.height() <= 0.f) {
        setErr(errOut, errLen, "skottie: invalid animation size");
        return nullptr;
    }
    const auto handle = new FRSkottieAnim();
    handle->fAnim = std::move(anim);
    return handle;
}

} // namespace

extern "C" {

FRSK_API FRSkottieAnim* frsk_load_data(const char* data, size_t len,
                                       char* errOut, size_t errLen)
{
    if (errOut && errLen > 0) { errOut[0] = '\0'; }
    if (!data || len == 0) {
        setErr(errOut, errLen, "skottie: empty data");
        return nullptr;
    }
    skottie::Animation::Builder builder;
    return buildAnim(builder, data, len, errOut, errLen);
}

FRSK_API FRSkottieAnim* frsk_load_file(const char* pathUtf8,
                                       const char* baseDirUtf8,
                                       char* errOut, size_t errLen)
{
    if (errOut && errLen > 0) { errOut[0] = '\0'; }
    if (!pathUtf8 || !*pathUtf8) {
        setErr(errOut, errLen, "skottie: empty path");
        return nullptr;
    }
    const std::string bytes = readFileBytes(pathUtf8);
    if (bytes.empty()) {
        setErr(errOut, errLen, std::string("skottie: cannot read ") + pathUtf8);
        return nullptr;
    }
    skottie::Animation::Builder builder;
    if (baseDirUtf8 && *baseDirUtf8) {
        builder.setResourceProvider(
                    skresources::FileResourceProvider::Make(
                        SkString(baseDirUtf8)));
    }
    return buildAnim(builder, bytes.data(), bytes.size(),
                     errOut, errLen);
}

FRSK_API void frsk_add_ref(FRSkottieAnim* anim)
{
    if (!anim) { return; }
    std::lock_guard<std::mutex> lock(anim->fMutex);
    ++anim->fRefs;
}

FRSK_API void frsk_release(FRSkottieAnim* anim)
{
    if (!anim) { return; }
    bool deleteMe = false;
    {
        std::lock_guard<std::mutex> lock(anim->fMutex);
        deleteMe = (--anim->fRefs <= 0);
    }
    if (deleteMe) { delete anim; }
}

FRSK_API double frsk_duration(const FRSkottieAnim* anim)
{
    return anim && anim->fAnim ? anim->fAnim->duration() : 0.0;
}

FRSK_API double frsk_fps(const FRSkottieAnim* anim)
{
    return anim && anim->fAnim ? anim->fAnim->fps() : 0.0;
}

FRSK_API int frsk_width(const FRSkottieAnim* anim)
{
    if (!anim || !anim->fAnim) { return 0; }
    return std::max(1, int(anim->fAnim->size().width() + 0.5f));
}

FRSK_API int frsk_height(const FRSkottieAnim* anim)
{
    if (!anim || !anim->fAnim) { return 0; }
    return std::max(1, int(anim->fAnim->size().height() + 0.5f));
}

FRSK_API SkImage* frsk_render(FRSkottieAnim* anim, double tSec)
{
    if (!anim || !anim->fAnim) { return nullptr; }
    // per-instance serialization: render tasks for different frames
    // of the same layer can be in flight at the same time
    std::lock_guard<std::mutex> lock(anim->fMutex);
    const auto& a = anim->fAnim;
    const double dur = a->duration();
    const double t = std::min(std::max(tSec, 0.0), dur);
    a->seekFrameTime(t);
    const auto& size = a->size();
    const int w = std::max(1, int(size.width() + 0.5f));
    const int h = std::max(1, int(size.height() + 0.5f));
    const auto surf = SkSurface::MakeRasterN32Premul(w, h);
    if (!surf) { return nullptr; }
    a->render(surf->getCanvas(), nullptr,
              skottie::Animation::kSkipTopLevelIsolation);
    // one reference for the caller; the sk_sp in the surface drops
    // its own on destruction, so release (not detach) here
    return surf->makeImageSnapshot().release();
}

FRSK_API const char* frsk_version()
{
    return "frictionskottie 1.0 (skottie)";
}

} // extern "C"
