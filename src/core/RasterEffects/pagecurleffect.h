/*
#
# Friction - https://friction.graphics
#
# Copyright (c) Ole-André Rodlie and contributors
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
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
*/

#ifndef PAGECURLEFFECT_H
#define PAGECURLEFFECT_H

#include "rastereffect.h"
#include "Animators/qrealanimator.h"

class ColorAnimator;
class ComboBoxProperty;

struct PageCurlEffectData {
    int mMode = 0;           // 0 = page curl, 1 = wave (image fully visible)
    float mProgress = 0.f;   // 0..1, 0 = flat passthrough
    float mDirection = 0.f;  // roll travel direction, degrees
    float mRadius = 0.12f;   // cylinder radius, fraction of image height
    float mBackR = 0.847f;
    float mBackG = 0.847f;
    float mBackB = 0.847f;
    float mLightAngle = 225.f; // degrees, 225 = from the upper left
    float mLightElev = 50.f;   // degrees above the page plane
    float mAmbient = 0.55f;
    float mShadow = 0.45f;
    float mSpecular = 0.25f;
    float mWaveAmp = 0.08f;  // wave mode: amplitude, fraction of image height
    float mWaveLen = 35.f;   // wave mode: wavelength, percent of the travel extent
    float mWavePhase = 0.f;  // wave mode: animation phase, radians
    int mTexW = 0;           // source size in pixels (0 = unknown)
    int mTexH = 0;
};

class PageCurlEffect : public RasterEffect {
public:
    PageCurlEffect();

    stdsptr<RasterEffectCaller> getEffectCaller(
            const qreal relFrame, const qreal resolution,
            const qreal influence, BoxRenderData * const data) const;
private:
    qsptr<ComboBoxProperty> mMode;
    qsptr<QrealAnimator> mProgress;
    qsptr<QrealAnimator> mDirection;
    qsptr<QrealAnimator> mRadius;
    qsptr<ColorAnimator> mBackColor;
    qsptr<QrealAnimator> mLightAngle;
    qsptr<QrealAnimator> mLightElev;
    qsptr<QrealAnimator> mAmbient;
    qsptr<QrealAnimator> mShadow;
    qsptr<QrealAnimator> mSpecular;
    qsptr<QrealAnimator> mWaveAmp;
    qsptr<QrealAnimator> mWaveLen;
    qsptr<QrealAnimator> mWaveSpeed;
};

#endif // PAGECURLEFFECT_H
