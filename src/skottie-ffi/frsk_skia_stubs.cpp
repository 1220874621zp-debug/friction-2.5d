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

// Local stand-ins for the few skia internals the skottie modules
// reference but the prebuilt skia.dll does not export. Everything in
// here stays private to this library.

#include "include/codec/SkCodec.h"
#include "include/core/SkData.h"
#include "include/core/SkImage.h"
#include "include/core/SkTypes.h"
#include "include/private/SkMalloc.h"
#include "include/utils/SkAnimCodecPlayer.h"
#include "src/core/SkOpts.h"
#include "src/core/SkSafeMath.h"
#include "src/core/SkTextBlobPriv.h"
#include "src/core/SkUtils.h"

#include <algorithm>
#include <cstring>

// ---------------------------------------------------------------------------
// SkOpts::hash_fn - the skottie/skresources modules only use this one
// function pointer out of the whole SkOpts dispatch table. The value
// never crosses the skia.dll boundary (hash tables are not shared),
// so any deterministic hash is safe; this is FNV-1a.
// ---------------------------------------------------------------------------
static uint32_t frsk_hash_portable(const void* data, size_t bytes,
                                   uint32_t seed)
{
    const uint8_t* p = static_cast<const uint8_t*>(data);
    uint32_t h = seed ? seed : 2166136261u;
    for (size_t i = 0; i < bytes; ++i) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

namespace SkOpts {
    uint32_t (*hash_fn)(const void*, size_t, uint32_t) = frsk_hash_portable;
}

// ---------------------------------------------------------------------------
// SkAnimCodecPlayer - upstream lives in src/utils/SkAnimCodecPlayer.cpp
// and depends on the internal SkCodecImageGenerator for its static-image
// branch; this reimplementation uses only the public codec API (decode
// the single frame into raster memory instead of wrapping the codec in
// a generator).
// ---------------------------------------------------------------------------

SkAnimCodecPlayer::SkAnimCodecPlayer(std::unique_ptr<SkCodec> codec)
    : fCodec(std::move(codec))
{
    fImageInfo = fCodec->getInfo();
    fFrameInfos = fCodec->getFrameInfo();
    fImages.resize(fFrameInfos.size());

    // change the interpretation of fDuration to an end-time for that frame
    size_t dur = 0;
    for (auto& f : fFrameInfos) {
        dur += f.fDuration;
        f.fDuration = dur;
    }
    fTotalDuration = dur;

    if (!fTotalDuration) {
        // static image: decode the only frame through the public api
        fFrameInfos.clear();
        fImages.clear();
        const size_t rb = fImageInfo.minRowBytes();
        const size_t size = fImageInfo.computeByteSize(rb);
        auto data = SkData::MakeUninitialized(size);
        if (size > 0
                && SkCodec::kSuccess == fCodec->getPixels(fImageInfo,
                                                          data->writable_data(),
                                                          rb)) {
            fImages.push_back(SkImage::MakeRasterData(fImageInfo,
                                                      std::move(data), rb));
        } else {
            fImages.push_back(nullptr);
        }
    }
}

SkAnimCodecPlayer::~SkAnimCodecPlayer() {}

SkISize SkAnimCodecPlayer::dimensions()
{
    return { fImageInfo.width(), fImageInfo.height() };
}

sk_sp<SkImage> SkAnimCodecPlayer::getFrameAt(int index)
{
    SkASSERT((unsigned)index < fFrameInfos.size());

    if (fImages[index]) { return fImages[index]; }

    const size_t rb = fImageInfo.minRowBytes();
    const size_t size = fImageInfo.computeByteSize(rb);
    auto data = SkData::MakeUninitialized(size);

    SkCodec::Options opts;
    opts.fFrameIndex = index;

    const int requiredFrame = fFrameInfos[index].fRequiredFrame;
    if (requiredFrame != SkCodec::kNoFrame) {
        auto requiredImage = fImages[requiredFrame];
        SkPixmap requiredPM;
        if (requiredImage && requiredImage->peekPixels(&requiredPM)) {
            sk_careful_memcpy(data->writable_data(), requiredPM.addr(), size);
            opts.fPriorFrame = requiredFrame;
        }
    }
    if (SkCodec::kSuccess == fCodec->getPixels(fImageInfo,
                                               data->writable_data(),
                                               rb, &opts)) {
        return fImages[index] = SkImage::MakeRasterData(fImageInfo,
                                                        std::move(data), rb);
    }
    return nullptr;
}

sk_sp<SkImage> SkAnimCodecPlayer::getFrame()
{
    SkASSERT(fTotalDuration > 0 || fImages.size() == 1);
    return fTotalDuration > 0
            ? this->getFrameAt(fCurrIndex)
            : fImages.front();
}

bool SkAnimCodecPlayer::seek(uint32_t msec)
{
    if (!fTotalDuration) { return false; }

    msec %= fTotalDuration;

    auto lower = std::lower_bound(fFrameInfos.begin(), fFrameInfos.end(),
                                  msec,
                                  [](const SkCodec::FrameInfo& info,
                                     uint32_t msec) {
                                      return (uint32_t)info.fDuration < msec;
                                  });
    const int prevIndex = fCurrIndex;
    fCurrIndex = int(lower - fFrameInfos.begin());
    return fCurrIndex != prevIndex;
}

// ---------------------------------------------------------------------------
// SkTextBlob run iteration - upstream lives in SkTextBlob.cpp, which
// drags in the whole text/glyph subsystem (strike cache, read/write
// buffers, GPU blobs). skottie only needs the run iterator walking
// logic below; the bodies mirror the upstream implementation using
// the public RunRecord API declared in SkTextBlobPriv.h.
// ---------------------------------------------------------------------------

// mirrors gScalarsPerPositioning (SkTextBlob.cpp): scalars stored per
// glyph for each positioning mode
static uint32_t frsk_scalars_per_positioning(const uint32_t pos)
{
    static const uint32_t scalars[4] = { 0, 1, 2, 4 };
    SkASSERT(pos <= 3);
    return scalars[pos];
}

size_t SkTextBlob::RunRecord::StorageSize(const uint32_t glyphCount,
                                          const uint32_t textSize,
                                          const SkTextBlob::GlyphPositioning positioning,
                                          SkSafeMath* safe)
{
    static_assert(SkIsAlign4(sizeof(SkScalar)), "SkScalar size alignment");

    const auto glyphSize = safe->mul(glyphCount, sizeof(uint16_t));
    const auto posSize = safe->mul(
                safe->mul(glyphCount,
                          frsk_scalars_per_positioning(
                              static_cast<uint32_t>(positioning))),
                sizeof(SkScalar));

    // RunRecord object + (aligned) glyph buffer + position buffer
    auto size = sizeof(SkTextBlob::RunRecord);
    size = safe->add(size, safe->alignUp(glyphSize, 4));
    size = safe->add(size, posSize);

    if (textSize) { // extended run
        size = safe->add(size, sizeof(uint32_t));
        size = safe->add(size, safe->mul(glyphCount, sizeof(uint32_t)));
        size = safe->add(size, textSize);
    }

    return safe->alignUp(size, sizeof(void*));
}

const SkTextBlob::RunRecord* SkTextBlob::RunRecord::First(const SkTextBlob* blob)
{
    // the first record (if present) follows the blob object, aligned
    // so the RunRecord is aligned too
    return reinterpret_cast<const RunRecord*>(
                SkAlignPtr(reinterpret_cast<uintptr_t>(blob + 1)));
}

const SkTextBlob::RunRecord* SkTextBlob::RunRecord::Next(const RunRecord* run)
{
    if (run->isLastRun()) { return nullptr; }
    // NextUnchecked inlined: storage advance by the current run size
    SkSafeMath safe;
    return reinterpret_cast<const RunRecord*>(
                reinterpret_cast<const uint8_t*>(run)
                + StorageSize(run->glyphCount(), run->textSize(),
                              run->positioning(), &safe));
}

void SkTextBlob::operator delete(void* p)
{
    sk_free(p);
}

void* SkTextBlob::operator new(size_t)
{
    SK_ABORT("All blobs are created by placement new.");
}

SkTextBlobRunIterator::SkTextBlobRunIterator(const SkTextBlob* blob)
    : fCurrentRun(SkTextBlob::RunRecord::First(blob)) {}

void SkTextBlobRunIterator::next()
{
    SkASSERT(!this->done());
    if (!this->done()) {
        fCurrentRun = SkTextBlob::RunRecord::Next(fCurrentRun);
    }
}

// referenced by the inline textSize()/clusterBuffer()/textBuffer()
// accessors; upstream body in SkTextBlob.cpp (PosCount inlined)
uint32_t* SkTextBlob::RunRecord::textSizePtr() const
{
    SkASSERT(isExtended());
    SkSafeMath safe;
    return const_cast<uint32_t*>(reinterpret_cast<const uint32_t*>(
                &this->posBuffer()[safe.mul(
                    fCount,
                    frsk_scalars_per_positioning(
                        static_cast<uint32_t>(positioning())))]));
}
