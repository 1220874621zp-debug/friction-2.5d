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
#include "glcallwrap.h"
#include "Private/esettings.h"
#include "colorhelpers.h"
#include <QPainter>
#include <QDebug>
#include "exceptions.h"

GLWindow::GLWindow(QWidget * const parent)
    : QOpenGLWidget(parent) {
    setUpdateBehavior(QOpenGLWidget::NoPartialUpdate);
}

QSize GLWindow::queryFramebufferDeviceSize() {
    // Measure the widget FBO instead of guessing from widget metrics:
    // FBO-name recycling defeats id checks, and Qt6's resize choreography
    // can serve paintGL with a stale width() - both were observed leaving
    // the skia surface one resize behind the real framebuffer.
    const GLuint fbo = defaultFramebufferObject();
    GLint objType = 0, objName = 0;
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glGetFramebufferAttachmentParameteriv(
                GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &objType);
    glGetFramebufferAttachmentParameteriv(
                GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &objName);
    QSize result;
    if (objType == GL_RENDERBUFFER) {
        GLint w = 0, h = 0;
        glBindRenderbuffer(GL_RENDERBUFFER, GLuint(objName));
        glGetRenderbufferParameteriv(GL_RENDERBUFFER,
                                     GL_RENDERBUFFER_WIDTH, &w);
        glGetRenderbufferParameteriv(GL_RENDERBUFFER,
                                     GL_RENDERBUFFER_HEIGHT, &h);
        result = QSize(int(w), int(h));
    } else if (objType == GL_TEXTURE) {
        GLint w = 0, h = 0;
        glBindTexture(GL_TEXTURE_2D, GLuint(objName));
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h);
        result = QSize(int(w), int(h));
    }
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    return result;
}

void GLWindow::bindSkia(const int w, const int h) {
    // Prefer the FBO's real color-attachment size: deriving it from
    // width()*devicePixelRatioF() disagrees with Qt's own FBO allocation
    // by rounding and can lag the widget by one resize, leaving a margin
    // band the surface never covers.
    QSize deviceSize = queryFramebufferDeviceSize();
    if (!deviceSize.isValid()) {
        deviceSize = QSize(qRound(w*devicePixelRatioF()),
                           qRound(h*devicePixelRatioF()));
    }
    GrGLFramebufferInfo fbInfo;
    fbInfo.fFBOID = context()->defaultFramebufferObject();//buffer;
    fbInfo.fFormat = GR_GL_RGBA8;//buffer;
    GrBackendRenderTarget backendRT = GrBackendRenderTarget(
                                        deviceSize.width(), deviceSize.height(),
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
    mBoundFboId = fbInfo.fFBOID;
    mBoundDeviceSize = deviceSize;
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
        if(!initializeOpenGLFunctions())
            RuntimeThrow("Initializing OpenGL 3.3 functions failed. "
                         "Make sure your GPU supports OpenGL 3.3.");
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
        // Qt6 calls initializeGL() BEFORE the widget FBO exists (Qt5
        // created it first): binding skia to FBO 0 here leaves the surface
        // permanently broken on Intel/core (every paint targets a stale
        // FBO -> flicker). Defer to the first paintGL, where the widget
        // FBO is guaranteed to be current.
        mDeferInit = true;
        mRebind = true;
#else
        initialize();
#endif
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
    // optional per-call GL error attribution ([glcall] <fn> err=<code>),
    // disabled unless FRICTION_GL_WRAP_ON=1 (per-call glGetError is too
    // costly to keep on in production)
    const auto wrappedIface = frictionWrapGlInterfaceForDiagnostics(iface);

    GrContextOptions options;
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
    // The old MSAA-resolve suspicion was a misdiagnosis of the per-frame
    // GL-state poisoning fixed by resetContext() in paintGL - restore the
    // configured sample count; FRICTION_SKIA_MSAA0 stays as an emergency
    // off-switch pending re-verification on Intel/core.
    options.fInternalMultisampleCount =
            qEnvironmentVariableIsSet("FRICTION_SKIA_MSAA0")
            ? 0 : eSettings::instance().fInternalMultisampleCount;
#else
    options.fInternalMultisampleCount = eSettings::instance().fInternalMultisampleCount;
#endif

    mGrContext = GrContext::MakeGL(wrappedIface, options);
    if (!mGrContext) { RuntimeThrow("Failed to make GrContext."); }

    try {
        bindSkia(width(), height());
    } catch(...) {
        RuntimeThrow("Failed to bind skia.");
    }
}

void GLWindow::paintGL() {
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
    // Frame boundary handled: the per-frame resetContext below resyncs
    // skia's HW cache, so no error-ancestry probing is needed here.
    // Deferred init: Qt6 calls initializeGL() before the widget FBO is
    // created, so GrContext/surface creation (which needs the FBO id) is
    // deferred to the first paint where defaultFramebufferObject() is valid.
    if (mDeferInit) {
        mDeferInit = false;
        mRebind = false;
        try {
            initialize();
        } catch(const std::exception& e) {
            gPrintExceptionCritical(e);
        }
        if (!mCanvas) { return; }
    }
    // Qt6's compositor may recreate the widget FBO outside resizeGL()
    // (hide/show, screen changes, high-DPI reparent). Skinny rebind guard:
    // compare the FBO id and its MEASURED attachment size - name recycling
    // and stale width() during resize choreography both defeat
    // computed-size checks and would leave the surface painting stale
    // framebuffer geometry (undrawn margin band during drag).
    const GLuint curFbo = defaultFramebufferObject();
    const QSize curSize = queryFramebufferDeviceSize();
    if (curFbo != mBoundFboId || curSize != mBoundDeviceSize) {
        mRebind = true;
    }
#endif
    if(mRebind) {
        mRebind = false;
        try {
            bindSkia(width(), height());
        } catch(const std::exception& e) {
            gPrintExceptionCritical(e);
        }
    }
    // cleared by Canvas
    // glClear(GL_COLOR_BUFFER_BIT);
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
    // Qt6's QOpenGLWidgetPrivate::render() resets program/ARRAY_BUFFER/blend
    // state on this context right before EVERY paintGL (Qt5 never did).
    // Skia's HW-state cache still believes its bindings are current, skips
    // the vertex-buffer rebind, and the per-flush geometry upload
    // (glBufferData+glBufferSubData in GrGLBuffer::onUpdateData) dies with
    // GL_INVALID_OPERATION -> that flush draws with stale/uninitialized
    // buffer content (diagonal garble during live redraw). Re-sync Skia's
    // cache every frame; the reset is lazy and lands before the first GL
    // call of the coming flush.
    if (mGrContext) { mGrContext->resetContext(kAll_GrBackendState); }
#endif
    renderSk(mCanvas);
    mCanvas->flush();
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
