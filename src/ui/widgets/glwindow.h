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

#ifndef GLWINDOW_H
#define GLWINDOW_H

#include "ui_global.h"

#include <QOpenGLWidget>
#include "glhelpers.h"
#include <string>

#include "skia/skiaincludes.h"

#include <QResizeEvent>
#include <QOpenGLPaintDevice>

struct ShaderEffectCreator;
struct ShaderEffectProgram;

class UI_EXPORT GLWindow : public QOpenGLWidget, protected QGL33 {
public:
    GLWindow(QWidget * const parent = nullptr);
protected:
    virtual void renderSk(SkCanvas * const canvas) = 0;
    void resizeGL(int, int) final;
    void initializeGL() final;
    void paintGL() final;
    void showEvent(QShowEvent *e);

    void initialize();
    void bindSkia(const int w, const int h);
    void updateFix();

    bool mRebind = false;
    // Qt6: initializeGL() runs BEFORE the widget FBO exists (Qt5 created
    // the FBO first); deferring GrContext/surface creation to the first
    // paintGL restores the Qt5 ordering
    bool mDeferInit = false;
    // Qt6: tracks which FBO/device size the Skia surface currently wraps;
    // paintGL rebinds whenever Qt silently recreates the widget FBO
    unsigned int mBoundFboId = ~0u;
    QSize mBoundDeviceSize;
    sk_sp<GrContext> mGrContext;
    sk_sp<SkSurface> mSurface;
    SkCanvas *mCanvas = nullptr;
    // Qt6 garble fix: Skia renders into a private offscreen FBO+texture
    // (with real depth24/stencil8), then paintGL does ONE atomic blit into
    // the Qt-managed widget FBO. This isolates Skia from the Qt6 compositor
    // / high-DPI resize path that reads the widget FBO mid-write and shows
    // recycled texels (diagonal color blocks).
    GLuint mOffscreenFbo = 0;
    GLuint mOffscreenTex = 0;
    GLuint mOffscreenDepthRbo = 0;
};

#endif // GLWINDOW_H
