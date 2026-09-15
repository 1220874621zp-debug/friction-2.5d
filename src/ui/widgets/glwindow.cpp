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
#include <QOpenGLFunctions>
#include "exceptions.h"

GLWindow::GLWindow(QWidget * const parent)
    : QOpenGLWidget(parent) {
    setUpdateBehavior(QOpenGLWidget::NoPartialUpdate);
}

void GLWindow::bindSkia(const int w, const int h) {
    qreal pixelRatio = devicePixelRatioF();
    int scaledWidth = pixelRatio*w;
    int scaledHeight = pixelRatio*h;
    GrGLFramebufferInfo fbInfo;
    fbInfo.fFBOID = context()->defaultFramebufferObject();//buffer;
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
    mBoundFboId = fbInfo.fFBOID;
    mBoundDeviceSize = QSize(scaledWidth, scaledHeight);
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

    GrContextOptions options;
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
    // Qt6/Windows + Intel core profile: Skia's internal MSAA resolve
    // (fInternalMultisampleCount default 4) silently fails when resolving
    // into the widget-FBO-managed surface - the drawn content reappears as
    // diagonal color bands only while the canvas is actively redrawn
    // (dragging invalidates the scene-frame cache -> direct skia draw ->
    // MSAA resolve path). Disable internal MSAA on Qt6; FRICTION_SKIA_MSAA0
    // kept as an A/B override.
    options.fInternalMultisampleCount =
            qEnvironmentVariableIsSet("FRICTION_SKIA_MSAA0")
            ? 0 : 0;
    qDebug() << "[glwin] Qt6: internalMultisampleCount forced 0 (MSAA resolve workaround)";
#else
    options.fInternalMultisampleCount = eSettings::instance().fInternalMultisampleCount;
#endif

    mGrContext = GrContext::MakeGL(iface, options);
    if (!mGrContext) { RuntimeThrow("Failed to make GrContext."); }

    try {
        bindSkia(width(), height());
    } catch(...) {
        RuntimeThrow("Failed to bind skia.");
    }
}

void GLWindow::paintGL() {
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
    // Frame-boundary error ancestry probe (Qt6 garble): every frame the
    // glprobe saw exactly 1 GL_INVALID_OPERATION even for a trivial rect.
    // Distinguish "our skia flush produced it" vs "Qt6 compositor left it
    // from the previous frame's post-paintGL compose pass": clear the flag
    // HERE, right before WE draw anything. If a probe later reports an
    // error again, it was generated inside OUR rendering; if the pre-clear
    // check already had one pending, the compositor dirtied it.
    QOpenGLFunctions* const qgl = context()->functions();
    int nPending = 0;
    while (qgl->glGetError() != GL_NO_ERROR && nPending < 32) nPending++;
    static int sPendingLog = 0;
    if (nPending && sPendingLog < 60) {
        sPendingLog++;
        qWarning() << "[glpre] paintGL entry had" << nPending
                   << "pending GL errors (from previous frame's compose?)";
    }
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
    // only re-wrap the surface when the FBO id or device size really
    // changed, otherwise rendering keeps painting into a stale FBO and the
    // repaint flickers between the new and old backing store.
    const GLuint curFbo = defaultFramebufferObject();
    const QSize curSize(qRound(width()*devicePixelRatioF()),
                        qRound(height()*devicePixelRatioF()));
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
