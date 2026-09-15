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
#include <QFile>
#include <QMutex>
#include <QDateTime>
#include "exceptions.h"

// Qt6 garbling investigation: GL errors from KHR_debug and actual FBO
// attachment state go to a file (qWarning/console is lost for a GUI app),
// so the exact illegal call can be read after the user reproduces the grarb.
namespace {
QMutex sGlDiagMutex;
void glDiagLog(const QString& line) {
    QMutexLocker lock(&sGlDiagMutex);
    QFile f(QStringLiteral("gl_diag.log"));
    if (f.open(QIODevice::WriteOnly | QIODevice::Append)) {
        f.write((QDateTime::currentDateTime().toString(Qt::ISODateWithMs) +
                 QStringLiteral(" ") + line + QStringLiteral("\n")).toUtf8());
    }
}
}

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

    // Render into OUR OWN offscreen FBO+texture, never directly into the
    // FBO Qt manages: Qt6's compositor / high-DPI resize path (exposed on
    // Intel core profile) reads the widget framebuffer while it is being
    // written, surfacing recycled/stale texels as diagonal color blocks
    // after a resize. Painting the whole scene into a private texture and
    // transferring it with ONE atomic blit at the end of paintGL isolates
    // Skia from the Qt widget-FBO lifecycle entirely.
    if (mOffscreenDepthRbo) { glDeleteRenderbuffers(1, &mOffscreenDepthRbo); mOffscreenDepthRbo = 0; }
    if (mOffscreenFbo) { glDeleteFramebuffers(1, &mOffscreenFbo); mOffscreenFbo = 0; }
    if (mOffscreenTex) { glDeleteTextures(1, &mOffscreenTex); mOffscreenTex = 0; }
    glGenTextures(1, &mOffscreenTex);
    glBindTexture(GL_TEXTURE_2D, mOffscreenTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, scaledWidth, scaledHeight,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    // GL_RGB8 wrapper so Skia's RGBA draw lands on RGB in the color buffer.
    // A real depth24+stencil8 renderbuffer is attached so Skia's declared
    // stencilBits matches an existing attachment - the missing-stencil
    // GL_INVALID_OPERATION / frozen dashed frame that sank the earlier
    // offscreen attempt is thereby avoided.
    glGenRenderbuffers(1, &mOffscreenDepthRbo);
    glBindRenderbuffer(GL_RENDERBUFFER, mOffscreenDepthRbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8,
                          scaledWidth, scaledHeight);
    glGenFramebuffers(1, &mOffscreenFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, mOffscreenFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, mOffscreenTex, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              GL_RENDERBUFFER, mOffscreenDepthRbo);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT,
                              GL_RENDERBUFFER, mOffscreenDepthRbo);
    const GLenum offscreenStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    // restore Qt's context state (Skia/CV must not inherit our FBO binding)
    glBindFramebuffer(GL_FRAMEBUFFER, context()->defaultFramebufferObject());
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    if (offscreenStatus != GL_FRAMEBUFFER_COMPLETE) {
        RuntimeThrow("Offscreen render target incomplete.");
    }

    const int stencilBits =
            qEnvironmentVariableIsSet("FRICTION_SKIA_NOSTENCIL") ? 0 : 8;
    GrGLFramebufferInfo fbInfo;
    fbInfo.fFBOID = mOffscreenFbo;//buffer;
    fbInfo.fFormat = GR_GL_RGBA8;//buffer;
    GrBackendRenderTarget backendRT = GrBackendRenderTarget(
                                        scaledWidth, scaledHeight,
                                        0, stencilBits, // (optional) 4, 8,
                                        fbInfo
                                        /*kRGBA_half_GrPixelConfig*/
                                        /*kSkia8888_GrPixelConfig*/);
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
    glDiagLog(QStringLiteral("[fbo] OFFSCREEN size=%1x%2 dpr=%3 status=0x%4 declaredStencil=%5 (Qt widget fboID=%6)")
              .arg(scaledWidth).arg(scaledHeight).arg(pixelRatio)
              .arg(int(offscreenStatus), 0, 16).arg(stencilBits)
              .arg(int(context()->defaultFramebufferObject())));
#endif

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
    mBoundFboId = mOffscreenFbo;
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
                glDiagLog(QStringLiteral("[gldebug] id=%1 %2")
                          .arg(id).arg(QString::fromUtf8(message ? message : "")));
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
    // (hide/show, screen changes). Our surface lives in our OWN offscreen
    // FBO, so it only depends on device size - rebind when that changes
    // (a size change is also when the Qt compositor/high-DPI resize path
    // used to show recycled-texel garble, so a fresh offscreen is right).
    const qreal pr = devicePixelRatioF();
    const QSize devSize(qRound(width()*pr), qRound(height()*pr));
    if (devSize != mBoundDeviceSize) {
        qDebug() << "[glwin] device size changed, rebinding skia:"
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
    // Present the private offscreen frame with ONE atomic blit into the
    // Qt-managed widget FBO. Skia never writes the widget FBO directly, so
    // its leftover GL state cannot corrupt what the compositor reads.
    if (mOffscreenFbo) {
        const qreal dpr = devicePixelRatioF();
        const int pw = qRound(dpr*width());
        const int ph = qRound(dpr*height());
        glDisable(GL_SCISSOR_TEST); // Skia left it enabled with a stale
                                    // small rect -> would clip the blit
        glBindFramebuffer(GL_READ_FRAMEBUFFER, mOffscreenFbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, defaultFramebufferObject());
        glBlitFramebuffer(0, 0, pw, ph, 0, 0, pw, ph,
                          GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
        // Qt6's widget compositor may read the widget FBO on a different
        // thread / on the next DWM present right after paintGL returns.
        // Without a barrier the blit above may still be sitting in the GL
        // command queue, so the compositor ingests a half-blitted (stale /
        // partially-written) frame -> the top-left-bright / bottom-right
        // dark diagonal gradient garble that only healed on re-expose.
        // Force the blit to have been executed before handing back to Qt.
        glFinish();
    }
    // Qt6's RHI widget compositor blits THIS widget FBO to the screen right
    // after paintGL returns. Skia flushes while leaving global GL state
    // behind (own backing FBO bound, and GL_SCISSOR_TEST enabled with the
    // last op's small rect). The compositor inherits that state: its
    // full-widget blit gets scissored to a sub-region and the untouched
    // area shows a recycled texture from unrelated widgets - the diagonal
    // colored garbling that only healed on re-expose. Restore a clean
    // full-widget state before handing back to Qt.
    const auto dfo = defaultFramebufferObject();
    glBindFramebuffer(GL_FRAMEBUFFER, dfo);
    const qreal dpr2 = devicePixelRatioF();
    glViewport(0, 0, qRound(width()*dpr2), qRound(height()*dpr2));
    glDisable(GL_SCISSOR_TEST);
    glScissor(0, 0, qRound(width()*dpr2), qRound(height()*dpr2));
    // re-declare the fbo so the rebind guard is coherent next paint
    if (dfo != mBoundFboId) { mBoundFboId = dfo; }
#endif
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
    // anomaly telemetry (throttled): a persistent gl error or incomplete
    // framebuffer here means the garbling survived the rebind guard
    const GLenum glErr = glGetError();
    const GLenum fbStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (glErr != GL_NO_ERROR || fbStatus != GL_FRAMEBUFFER_COMPLETE) {
        static int sWarnCount = 0;
        if (sWarnCount < 10 || sWarnCount % 120 == 0)
            qWarning() << "[glwin] anomaly: glErr" << int(glErr)
                       << "fbStatus" << int(fbStatus)
                       << "fbo" << defaultFramebufferObject();
        if (sWarnCount < 10)
            glDiagLog(QStringLiteral("[anomaly] glErr=%1 fbStatus=0x%2 fbo=%3")
                      .arg(int(glErr)).arg(int(fbStatus), 0, 16)
                      .arg(int(defaultFramebufferObject())));
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
