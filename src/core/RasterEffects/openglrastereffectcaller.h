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

// Fork of enve - Copyright (C) 2016-2020 Maurycy Liebner

#ifndef OPENGLRASTEREFFECTCALLER_H
#define OPENGLRASTEREFFECTCALLER_H

#include "rastereffect.h"

class CORE_EXPORT OpenGLRasterEffectCaller : public RasterEffectCaller {
protected:
    OpenGLRasterEffectCaller(bool& initialized,
                             GLuint& programId,
                             const QString& shaderPath,
                             const HardwareSupport hwSupport,
                             const bool forceMargin = false,
                             const QMargins& margin = QMargins());

    virtual void iniVars(QGL33 * const gl) const = 0;
    virtual void setVars(QGL33 * const gl) const = 0;
public:
    void processGpu(QGL33 * const gl, GpuRenderTools &renderTools) final;

    // a shader that failed to compile/link on this machine is remembered for
    // the session: callers using it are created CPU-only from the start, so a
    // single GPU failure costs one warning instead of one per frame
    void onGpuFailure() override;
    static bool sShaderFailed(const QString& path);
    static void sMarkShaderFailed(const QString& path);
private:
    void iniProgram(QGL33 * const gl);

    bool& mInitialized;
    GLuint& mProgramId;
    const QString mShaderPath;
};

#endif // OPENGLRASTEREFFECTCALLER_H
