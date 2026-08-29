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

// Chroma key algorithm ported from Enhanced Hybrid Keyer 3.1
// https://github.com/RazvanO2/Enhanced-Hybrid-Keyer
// Copyright (c) Eki "Halsu" Halkka and Razvan "zvix" Olariu (MIT License)

#ifndef CHROMAKEYEFFECT_H
#define CHROMAKEYEFFECT_H

#include "rastereffect.h"
#include "Animators/qrealanimator.h"

class ColorAnimator;
class ComboBoxProperty;

struct ChromaKeyEffectData {
    float mKeyR = 0.f;
    float mKeyG = 1.f;
    float mKeyB = 0.f;
    int mKeyMethod = 0;
    float mTolerance = 50.f;
    float mMatteBlack = 0.f;
    float mMatteWhite = 0.f;
    float mMatteHighlights = 0.f;
    float mMatteShadows = 0.f;
    float mEdgeSoftness = 0.f;
    float mHairDetail = 0.f;
    float mDefringe = 0.f;
    float mSpillReduction = 50.f;
    float mSpillBalance = 0.f;
    int mPreviewMode = 0;
};

class ChromaKeyEffect : public RasterEffect {
public:
    ChromaKeyEffect();

    stdsptr<RasterEffectCaller> getEffectCaller(
            const qreal relFrame, const qreal resolution,
            const qreal influence, BoxRenderData * const data) const;
private:
    qsptr<ColorAnimator> mKeyColor;
    qsptr<ComboBoxProperty> mKeyMethod;
    qsptr<QrealAnimator> mTolerance;
    qsptr<QrealAnimator> mMatteBlack;
    qsptr<QrealAnimator> mMatteWhite;
    qsptr<QrealAnimator> mMatteHighlights;
    qsptr<QrealAnimator> mMatteShadows;
    qsptr<QrealAnimator> mEdgeSoftness;
    qsptr<QrealAnimator> mHairDetail;
    qsptr<QrealAnimator> mDefringe;
    qsptr<QrealAnimator> mSpillReduction;
    qsptr<QrealAnimator> mSpillBalance;
    qsptr<ComboBoxProperty> mPreviewMode;
};

#endif // CHROMAKEYEFFECT_H
