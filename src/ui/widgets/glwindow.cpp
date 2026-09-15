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
    // recreateFbos() emits resized() even when the widget size did not
    // change - make sure the skia surface follows the new FBO
    connect(this, &QOpenGLWidget::resized, this, [this]() { mRebind = true; });
#else
    setUpdateBehavior(QOpenGLWidget::NoPartialUpdate);
#endif
}

void GLWindow::bindSkia(const int w, const int h) {
    qreal pixelRatio = devicePixelRatioF();
    // Qt computes the widget FBO size with rounding (QSize * qreal);
    // truncating made the surface 1px shorter than the FBO at fractional
    // dpr (e.g. 805 * 1.5 = 1207.5 -> Qt 1208 vs skia 1207)
    int scaledWidth = qRound(pixelRatio*w);
    int scaledHeight = qRound(pixelRatio*h);
    GrGLFramebufferInfo fbInfo;
    fbInfo.fFBOID = context()->defaultFramebufferObject();//buffer;
    fbInfo.fFormat = GR_GL_RGBA8;//buffer;
    const int stencilBits =
            qEnvironmentVariableIsSet("FRICTION_SKIA_NOSTENCIL") ? 0 : 8;
    GrBackendRenderTarget backendRT = GrBackendRenderTarget(
                                        scaledWidth, scaledHeight,
                                        0, stencilBits, // (optional) 4, 8,
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

#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
        // Qt6 invokes initializeGL() before the widget FBO exists
        // (Qt5 created it first); initialize() against FBO 0 leaves skia
        // broken on intel/core-profile - defer to the first paintGL
        mDeferInit = true;
        mRebind = true;
        const auto& fmt = context()->format();
        qDebug() << "[glwin] ctx format" << fmt.majorVersion() << "."
                 << fmt.minorVersion() << "profile" << fmt.profile()
                 << "samples" << fmt.samples() << "depth" << fmt.depthBufferSize()
                 << "stencil" << fmt.stencilBufferSize()
                 << "rgba" << fmt.redBufferSize() << fmt.greenBufferSize()
                 << fmt.blueBufferSize() << fmt.alphaBufferSize();
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

#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
    // garbling diagnostics: the driver names the exact invalid call,
    // no more guessing which skia op fails
    qDebug() << "[glwin] renderer"
             << reinterpret_cast<const char*>(glGetString(GL_RENDERER))
             << "version"
             << reinterpret_cast<const char*>(glGetString(GL_VERSION));
    if (context()->hasExtension(QByteArrayLiteral("GL_KHR_debug")) ||
        context()->hasExtension(QByteArrayLiteral("GL_ARB_debug_output"))) {
        // GL callback calling convention (APIENTRY not pulled in by Qt)
#if defined(Q_OS_WIN)
        using DebugCb = void (__stdcall*)(unsigned int, unsigned int,
                                          unsigned int, unsigned int,
                                          int, const char*, const void*);
        using DebugMessageCallbackFn = void (__stdcall*)(DebugCb,
                                                         const void*);
#else
        using DebugCb = void (*)(unsigned int, unsigned int,
                                 unsigned int, unsigned int,
                                 int, const char*, const void*);
        using DebugMessageCallbackFn = void (*)(DebugCb, const void*);
#endif
        const auto pCallback = reinterpret_cast<DebugMessageCallbackFn>(
                    context()->getProcAddress("glDebugMessageCallback"));
        if (pCallback) {
            glEnable(0x92E0);           // GL_DEBUG_OUTPUT
            glEnable(0x8242);           // GL_DEBUG_OUTPUT_SYNCHRONOUS
            pCallback([](unsigned int, unsigned int type, unsigned int id,
                         unsigned int, int, const char* message,
                         const void*) {
                if (type != 0x824C) return; // GL_DEBUG_TYPE_ERROR only
                static int sCount = 0;
                if (sCount >= 40) return;
                sCount++;
                qWarning() << "[gldebug] id" << id << ":" << message;
            }, nullptr);
            qDebug() << "[glwin] KHR_debug callback installed";
        }
    }
#endif

    const auto iface = GrGLMakeNativeInterface();
    if (!iface) { RuntimeThrow("Failed to make native interface."); }

    GrContextOptions options;
    // env kill-switches for the garbling investigation:
    // FRICTION_SKIA_MSAA0=1 disables skia's internal msaa,
    // FRICTION_SKIA_NOSTENCIL=1 reports a stencil-less render target
    options.fInternalMultisampleCount =
            qEnvironmentVariableIsSet("FRICTION_SKIA_MSAA0")
            ? 0 : eSettings::instance().fInternalMultisampleCount;

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
    // (hide/show, screen changes). A skia surface still wrapping the old
    // FBO id then renders into a recycled texture name, which shows up as
    // garbled content from unrelated widgets - detect and rebind.
    const auto fboId = defaultFramebufferObject();
    const qreal pr = devicePixelRatioF();
    const QSize devSize(qRound(width()*pr), qRound(height()*pr));
    if (fboId != mBoundFboId || devSize != mBoundDeviceSize) {
        qDebug() << "[glwin] fbo/size changed, rebinding skia:"
                 << mBoundFboId << "->" << fboId
                 << mBoundDeviceSize << "->" << devSize;
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
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
    // anomaly telemetry (throttled): a persistent gl error or incomplete
    // framebuffer here means the garbling survived the rebind guard
    const GLenum glErr = glGetError();
    const GLenum fbStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (glErr != GL_NO_ERROR || fbStatus != GL_FRAMEBUFFER_COMPLETE) {
        static int sWarnCount = 0;
        if (sWarnCount < 10 || sWarnCount % 120 == 0)
            qWarning() << "[glwin] anomaly: glErr" << int(glErr)
                       << "fbStatus" << int(fbStatus) << "fbo" << fboId;
        sWarnCount++;
    }
#endif
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
