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

#include "openglrastereffectcaller.h"

#include <QSet>

namespace {
// shader resource paths that failed to compile/link on this machine
QSet<QString>& failedShaders() {
    static QSet<QString> sFailed;
    return sFailed;
}
}

bool OpenGLRasterEffectCaller::sShaderFailed(const QString& path) {
    return failedShaders().contains(path);
}

void OpenGLRasterEffectCaller::sMarkShaderFailed(const QString& path) {
    failedShaders().insert(path);
}

void OpenGLRasterEffectCaller::onGpuFailure() {
    sMarkShaderFailed(mShaderPath);
    fallBackToCpu();
}

OpenGLRasterEffectCaller::OpenGLRasterEffectCaller(
        bool& initialized,
        GLuint& programId,
        const QString& shaderPath,
        const HardwareSupport hwSupport,
        const bool forceMargin,
        const QMargins& margin) :
    RasterEffectCaller(sShaderFailed(shaderPath) ?
                           HardwareSupport::cpuOnly : hwSupport,
                       forceMargin, margin),
    mInitialized(initialized),
    mProgramId(programId),
    mShaderPath(shaderPath) {}

void OpenGLRasterEffectCaller::processGpu(QGL33* const gl, GpuRenderTools& renderTools) {
    renderTools.switchToOpenGL(gl);

    if(!mInitialized) {
        iniProgram(gl);
        mInitialized = true;
    }

    renderTools.requestTargetFbo().bind(gl);
    gl->glClear(GL_COLOR_BUFFER_BIT);

    gl->glUseProgram(mProgramId);

    setVars(gl);

    gl->glActiveTexture(GL_TEXTURE0);
    renderTools.getSrcTexture().bind(gl);

    gl->glBindVertexArray(renderTools.getSquareVAO());
    gl->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    renderTools.swapTextures();
}

void OpenGLRasterEffectCaller::iniProgram(QGL33* const gl) {
    try {
        gIniProgram(gl, mProgramId, GL_TEXTURED_VERT, mShaderPath);
    } catch(const std::exception& e) {
        // keep the underlying reason (missing resource vs a GLSL error) so
        // the debug log says what actually failed
        RuntimeThrow(QString("Could not initialize a program for '%1': %2")
                     .arg(mShaderPath, QString::fromUtf8(e.what())));
    }

    gl->glUseProgram(mProgramId);

    const auto texLocation = gl->glGetUniformLocation(mProgramId, "tex");
    gl->glUniform1i(texLocation, 0);

    iniVars(gl);
}
