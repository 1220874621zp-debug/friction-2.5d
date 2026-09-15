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

#include "glwindow.h"
#include "Private/esettings.h"
#include "colorhelpers.h"
#include <QPainter>
#include <QDebug>
#include "exceptions.h"

GLWindow::GLWindow(QWidget * const parent)
    : QOpenGLWidget(parent) {
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
    // Qt6/Windows: with NoPartialUpdate the compositor's FBO lifecycle
    // (DWM flip model) glitches under rapid repaints - the canvas starts
    // showing textures from unrelated widgets (diagonal color blocks)
    // until the window is re-exposed. PartialUpdate keeps Qt's blit-based
    // path, which stays stable; the canvas fully redraws each frame so
    // preserving old content costs nothing.
    setUpdateBehavior(QOpenGLWidget::PartialUpdate);
#else
    setUpdateBehavior(QOpenGLWidget::NoPartialUpdate);
#endif
}

void GLWindow::bindSkia(const int w, const int h) {
    qreal pixelRatio = devicePixelRatioF();
    int scaledWidth = pixelRatio*w;
    int scaledHeight = pixelRatio*h;

    // render into our OWN offscreen fbo+texture, never directly into the
    // fbo Qt manages: Qt6's compositor may read the widget framebuffer
    // while it is being written (transient garbling during continuous
    // repaints, i.e. canvas dragging). Painting the whole scene into a
    // private texture and transferring it with ONE atomic blit at the
    // end of paintGL shrinks that race window to a single GPU copy.
    if (mOffscreenFbo) { glDeleteFramebuffers(1, &mOffscreenFbo); mOffscreenFbo = 0; }
    if (mOffscreenTex) { glDeleteTextures(1, &mOffscreenTex); mOffscreenTex = 0; }
    if (mOffscreenStencil) { glDeleteRenderbuffers(1, &mOffscreenStencil); mOffscreenStencil = 0; }
    glGenTextures(1, &mOffscreenTex);
    glBindTexture(GL_TEXTURE_2D, mOffscreenTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, scaledWidth, scaledHeight,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    // skia is told the target has 8 stencil bits (see GrBackendRenderTarget
    // below) and dashed strokes do stencil-based path rendering: the
    // attachment MUST exist or drivers hang on the stencil ops (safe
    // frames toggle froze the app before this)
    glGenRenderbuffers(1, &mOffscreenStencil);
    glBindRenderbuffer(GL_RENDERBUFFER, mOffscreenStencil);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8,
                          scaledWidth, scaledHeight);
    glGenFramebuffers(1, &mOffscreenFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, mOffscreenFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, mOffscreenTex, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              GL_RENDERBUFFER, mOffscreenStencil);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT,
                              GL_RENDERBUFFER, mOffscreenStencil);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        glBindFramebuffer(GL_FRAMEBUFFER, context()->defaultFramebufferObject());
        RuntimeThrow("Failed to create offscreen render target.");
    }
    glBindFramebuffer(GL_FRAMEBUFFER, context()->defaultFramebufferObject());

    GrGLFramebufferInfo fbInfo;
    fbInfo.fFBOID = mOffscreenFbo;
    fbInfo.fFormat = GR_GL_RGBA8;//buffer;
    GrBackendRenderTarget backendRT = GrBackendRenderTarget(
                                        scaledWidth, scaledHeight,
                                        0, 8, // (optional) 4, 8,
                                        fbInfo
                                        /*kRGBA_half_GrPixelConfig*/
                                        /*kSkia8888_GrPixelConfig*/);

    // setup SkSurface
    // To use distance field text, use commented out SkSurfaceProps instead
    // SkSurfaceProps props(SkSurfaceProps::kUseDeviceIndependentFonts_Flag,
    //                      SkSurfaceProps::kLegacyFontHost_InitType);
    SkSurfaceProps props(SkSurfaceProps::kLegacyFontHost_InitType);

//    sk_sp<SkColorSpace> colorSpace = SkColorSpace::MakeSRGB();
    mSurface = SkSurface::MakeFromBackendRenderTarget(
                                    mGrContext.get(),
                                    backendRT,
                                    kBottomLeft_GrSurfaceOrigin,
                                    SkColorType::kRGBA_8888_SkColorType,
                                    nullptr/*colorSpace*/,
                                    &props);
    if(!mSurface) RuntimeThrow("Failed to wrap buffer into SkSurface.");
    mCanvas = mSurface->getCanvas();
    mGrContext->resetContext();
}

void GLWindow::resizeGL(int, int) {
    try {
        mRebind = true;
        update();
    } catch(const std::exception& e) {
        gPrintExceptionCritical(e);
    }
}

void GLWindow::initializeGL() {
    try {
        const auto globalCtx = QOpenGLContext::globalShareContext();
        if(!globalCtx)
            RuntimeThrow("Application-wide shared OpenGL context not found");
        if(context()->shareContext() != globalCtx)
            context()->setShareContext(globalCtx);

#ifdef USE_GLES
        if (!context() || !context()->isValid()) {
            RuntimeThrow("Initializing OpenGL ES failed. Context is invalid. "
                         "Make sure your GPU supports OpenGL ES 3.0.");
        }
        initializeOpenGLFunctions();
#else
        if (!initializeOpenGLFunctions()) {
            RuntimeThrow("Initializing OpenGL 3.3 functions failed. "
                         "Make sure your GPU supports OpenGL 3.3.");
        }
#endif

        initialize();
    } catch(const std::exception& e) {
        gPrintExceptionFatal(e);
    }
}

void GLWindow::initialize()
{
    glClearColor(0, 0, 0, 1);

    glEnable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    const auto iface = GrGLMakeNativeInterface();
    if (!iface) { RuntimeThrow("Failed to make native interface."); }

    GrContextOptions options;
    options.fInternalMultisampleCount = eSettings::instance().fInternalMultisampleCount;

    mGrContext = GrContext::MakeGL(iface, options);
    if (!mGrContext) { RuntimeThrow("Failed to make GrContext."); }

    try {
        bindSkia(width(), height());
    } catch(...) {
        RuntimeThrow("Failed to bind skia.");
    }
}

void GLWindow::paintGL() {
    if(mRebind) {
        mRebind = false;
        try {
            bindSkia(width(), height());
        } catch(const std::exception &e) {
            gPrintExceptionCritical(e);
        }
    }
    // cleared by Canvas
    // glClear(GL_COLOR_BUFFER_BIT);
    renderSk(mCanvas);
    mCanvas->flush();

    // single atomic transfer of the finished frame into the fbo Qt
    // composites (see bindSkia): the long scene render stays invisible
    // to the compositor, only this one blit races with it
    if (mOffscreenFbo) {
        const qreal dpr = devicePixelRatioF();
        const int pw = qCeil(dpr*width());
        const int ph = qCeil(dpr*height());
        GLint boundFbo = 0;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &boundFbo);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, mOffscreenFbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, boundFbo);
        glBlitFramebuffer(0, 0, pw, ph, 0, 0, pw, ph,
                          GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glBindFramebuffer(GL_FRAMEBUFFER, GLuint(boundFbo));
    }
}

void GLWindow::showEvent(QShowEvent *e) {
    resizeGL(width(), height());
    updateFix();
    QOpenGLWidget::showEvent(e);
}

#include <QTimer>
void GLWindow::updateFix() {
    QTimer::singleShot(1, this, [this]() { update(); });
}
