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
#include <QDir>
#include <QMutex>
#include <QDateTime>
#include <vector>
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

// Qt6/Windows high-DPI: the RHI compositor creates the widget FBO with a
// physical size that can differ by 1-2 px from any guess based on
// qRound(logical*dpr) - it truncates with QSize(int(x), int(y)). Blitting
// an offscreen that is 1px larger overflows the destination and yields
// GL_INVALID_OPERATION (gl_diag spans with "[anomaly] glErr=1282" every
// frame) - the blit silently does nothing, Qt keeps compositing the stale
// widget FBO and the canvas shows diagonal recycled-texel garble. Query
// the real color-attachment size instead of assuming.
bool queryWidgetFboPhysSize(QGL33* const gl,
                            const GLuint fbo, GLsizei& w, GLsizei& h) {
    w = 0; h = 0;
    GLint prevRead = 0, prevDraw = 0;
    gl->glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevRead);
    gl->glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDraw);
    gl->glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
    gl->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo);
    GLint type = 0;
    gl->glGetFramebufferAttachmentParameteriv(
                GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &type);
    GLint name = 0;
    gl->glGetFramebufferAttachmentParameteriv(
                GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &name);
    bool ok = false;
    if (type == GL_RENDERBUFFER) {
        gl->glBindRenderbuffer(GL_RENDERBUFFER, GLuint(name));
        GLint rw = 0, rh = 0;
        gl->glGetRenderbufferParameteriv(GL_RENDERBUFFER,
                                         GL_RENDERBUFFER_WIDTH, &rw);
        gl->glGetRenderbufferParameteriv(GL_RENDERBUFFER,
                                         GL_RENDERBUFFER_HEIGHT, &rh);
        w = rw; h = rh; ok = (rw > 0 && rh > 0);
    } else if (type == GL_TEXTURE) {
        gl->glBindTexture(GL_TEXTURE_2D, GLuint(name));
        GLint tw = 0, th = 0;
        gl->glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &tw);
        gl->glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &th);
        w = tw; h = th; ok = (tw > 0 && th > 0);
        gl->glBindTexture(GL_TEXTURE_2D, 0);
    }
    gl->glBindFramebuffer(GL_READ_FRAMEBUFFER, GLuint(prevRead));
    gl->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, GLuint(prevDraw));
    return ok;
}
}

GLWindow::GLWindow(QWidget * const parent)
    : QOpenGLWidget(parent) {
    // Qt6: official upstream friction qt6 branch uses NoPartialUpdate with
    // full repaints. PartialUpdate made Qt6's compositor upload only the
    // dirty-rect-listed region from the widget FBO and keep stale cached
    // textures for the rest - with Skia repainting the WHOLE canvas every
    // frame the untouched regions stayed recycled (diagonal color blocks
    // that only healed on re-expose). NoPartialUpdate forces a full
    // per-frame texture upload so the compositor always displays fresh FBO
    // content.
    setUpdateBehavior(QOpenGLWidget::NoPartialUpdate);
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
    // recreateFbos() emits resized() even when the widget size did not
    // change - make sure the skia surface follows the new FBO
    connect(this, &QOpenGLWidget::resized, this, [this]() { mRebind = true; });
    // Qt6 compositor draw order (qbackingstoredefaultcompositor.cpp):
    // texture widgets are drawn FIRST (NoBlend), then the raster backing
    // store is BLENDED OVER them. Stale opaque pixels the store holds in
    // this widget's rect (left behind by layout switches / the welcome
    // screen) then cover the canvas - the "other widgets' textures"
    // diagonal garble. Qt5 composited in the opposite order (GL texture
    // last) and was immune. StacksOnTop moves this widget's texture to
    // the final pass so nothing can be blended over it.
    setAttribute(Qt::WA_AlwaysStackOnTop);
#endif
}

void GLWindow::bindSkia(const int w, const int h) {
    qreal pixelRatio = devicePixelRatioF();
    // Qt computes the widget FBO size with rounding (QSize * qreal);
    // truncating made the surface 1px shorter than the FBO at fractional
    // dpr (e.g. 805 * 1.5 = 1207.5 -> Qt 1208 vs skia 1207)
    int scaledWidth = qRound(pixelRatio*w);
    int scaledHeight = qRound(pixelRatio*h);

#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
    // Qt6: the RHI compositor creates the widget FBO at a size that is
    // NOT reliably qRound(logical*dpr) - it truncates, and at fractional
    // dpr (1.5) the guess can be 1px larger than the real attachment.
    // Blitting that 1px-larger source overflows the destination FBO and
    // raises GL_INVALID_OPERATION, which silently aborts the blit: the
    // widget FBO keeps its stale contents and the compositor shows the
    // recycled-texel diagonal garble. Trust nothing: read the actual
    // color-attachment size of the bound widget FBO.
    const GLuint widgetFbo = context()->defaultFramebufferObject();
    GLsizei realW = 0, realH = 0;
    if (queryWidgetFboPhysSize(this, widgetFbo, realW, realH)) {
        scaledWidth = realW;
        scaledHeight = realH;
    }
    glDiagLog(QStringLiteral("[fbo] REAL widget fbo=%1 attach=%2x%3 (guess=%4x%5 dpr=%6)")
              .arg(int(widgetFbo)).arg(scaledWidth).arg(scaledHeight)
              .arg(qRound(pixelRatio*w)).arg(qRound(pixelRatio*h))
              .arg(pixelRatio));

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
    glDiagLog(QStringLiteral("[fbo] OFFSCREEN size=%1x%2 dpr=%3 status=0x%4 declaredStencil=%5 (Qt widget fboID=%6)")
              .arg(scaledWidth).arg(scaledHeight).arg(pixelRatio)
              .arg(int(offscreenStatus), 0, 16)
              .arg(qEnvironmentVariableIsSet("FRICTION_SKIA_NOSTENCIL") ? 0 : 8)
              .arg(int(context()->defaultFramebufferObject())));

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
#else
    // Qt5: original direct-render path - draw straight into Qt's widget
    // FBO (the pre-migration behaviour). Do NOT use the offscreen FBO
    // here: paintGL's blit is Qt6-only, so Qt5 would render into a
    // never-presented texture and show a blank canvas.
    GrGLFramebufferInfo fbInfo;
    fbInfo.fFBOID = context()->defaultFramebufferObject();//buffer;
    fbInfo.fFormat = GR_GL_RGBA8;//buffer;
    GrBackendRenderTarget backendRT = GrBackendRenderTarget(
                                        scaledWidth, scaledHeight,
                                        0, 8, // (optional) 4, 8,
                                        fbInfo
                                        /*kRGBA_half_GrPixelConfig*/
                                        /*kSkia8888_GrPixelConfig*/);
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
    // Compare against the REAL widget-FBO size: at fractional dpr the
    // qRound guess can differ from Qt's truncated attachment size, so a
    // stale guess would skip the rebind and keep blitting an offscreen
    // of the wrong size forever (persistent GL_INVALID_OPERATION).
    GLsizei ww = 0, wh = 0;
    if (!queryWidgetFboPhysSize(this, defaultFramebufferObject(), ww, wh) ||
            ww <= 0 || wh <= 0) {
        ww = qRound(width()*pr);
        wh = qRound(height()*pr);
    }
    const QSize devSize(ww, wh);
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
    // one-shot offscreen vs widget-FBO full readback: decide whether the
    // garble is already inside the offscreen (Skia/1282) or only appears
    // after Qt's compositor reads the widget FBO (timing/texture-id reuse)
    {
        static int sOffDumpFrames = 0;
        if (sOffDumpFrames < 3 && mOffscreenFbo) {
            sOffDumpFrames++;
            const QDir dir = QCoreApplication::applicationDirPath() +
                             QStringLiteral("/canvas_diag");
            dir.mkpath(QStringLiteral("."));
            const QString stamp = QString::number(sOffDumpFrames);
            const GLsizei dw = mBoundDeviceSize.width();
            const GLsizei dh = mBoundDeviceSize.height();
            if (dw > 0 && dh > 0) {
                std::vector<uchar> od(size_t(dw*dh*4));
                std::vector<uchar> wd(size_t(dw*dh*4));
                glBindFramebuffer(GL_READ_FRAMEBUFFER, mOffscreenFbo);
                glReadPixels(0, 0, dw, dh, GL_RGBA, GL_UNSIGNED_BYTE,
                             od.data());
                glBindFramebuffer(GL_READ_FRAMEBUFFER,
                                  defaultFramebufferObject());
                glReadPixels(0, 0, dw, dh, GL_RGBA, GL_UNSIGNED_BYTE,
                             wd.data());
                glBindFramebuffer(GL_READ_FRAMEBUFFER, mOffscreenFbo);
                QImage oi(od.data(), dw, dh, dw*4, QImage::Format_RGBA8888);
                QImage wi(wd.data(), dw, dh, dw*4, QImage::Format_RGBA8888);
                const QString base = dir.filePath(QStringLiteral("sb_%1_")
                                                  .arg(stamp));
                oi.mirrored().save(base + QStringLiteral("offscreen.png"));
                wi.mirrored().save(base + QStringLiteral("widget.png"));
                glDiagLog(QStringLiteral("[readback] frame=%1 %2x%3 saved offscreen vs widget")
                          .arg(stamp).arg(dw).arg(dh));
            }
        }
    }
    // Present the private offscreen frame with ONE atomic blit into the
    // Qt-managed widget FBO. Skia never writes the widget FBO directly, so
    // its leftover GL state cannot corrupt what the compositor reads.
    if (mOffscreenFbo) {
        // Blit using the REAL widget-FBO attachment size, never a guess:
        // at fractional dpr a qRound(logical*dpr) guess is 1px too big,
        // blitting it overflows the destination -> GL_INVALID_OPERATION
        // -> the blit is a no-op -> widget FBO stays stale -> compositor
        // shows the recycled-texel garble. (Same size already used when
        // the offscreen was created in bindSkia.)
        GLsizei bw = 0, bh = 0;
        const GLuint bfo = defaultFramebufferObject();
        if (!queryWidgetFboPhysSize(this, bfo, bw, bh) || bw <= 0 || bh <= 0) {
            bw = qRound(devicePixelRatioF()*width());
            bh = qRound(devicePixelRatioF()*height());
        }
        glDisable(GL_SCISSOR_TEST); // Skia left it enabled with a stale
                                    // small rect -> would clip the blit
        glBindFramebuffer(GL_READ_FRAMEBUFFER, mOffscreenFbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, bfo);
        glBlitFramebuffer(0, 0, bw, bh, 0, 0, bw, bh,
                          GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glBindFramebuffer(GL_FRAMEBUFFER, bfo);
        // Qt6's widget compositor may read the widget FBO on a different
        // thread / on the next DWM present right after paintGL returns.
        // Without a barrier the blit above may still be sitting in the GL
        // command queue, so the compositor ingests a half-blitted (stale /
        // partially-written) frame -> the top-left-bright / bottom-right
        // dark diagonal gradient garble that only healed on re-expose.
        // Force the blit to have been executed before handing back to Qt.
        glFinish();
    }
    // blit verification (throttled): read one pixel back from both the
    // offscreen source and the (blitted) widget FBO - if they differ, the
    // blit is being rejected by the driver and the compositor keeps
    // showing the stale widget FBO (the diagonal garble).
    {
        static int sBlitCheckFrames = 0;
        if (sBlitCheckFrames < 6 && mOffscreenFbo) {
            sBlitCheckFrames++;
            GLubyte src[4] = {0,0,0,0}, dst[4] = {0,0,0,0};
            const GLsizei cx = qMax(1, int(qRound(width()*
                                                 devicePixelRatioF())/2));
            const GLsizei cy = qMax(1, int(qRound(height()*
                                                 devicePixelRatioF())/2));
            glBindFramebuffer(GL_READ_FRAMEBUFFER, mOffscreenFbo);
            glReadPixels(cx, cy, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, src);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, defaultFramebufferObject());
            glReadPixels(cx, cy, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, dst);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, mOffscreenFbo);
            glDiagLog(QStringLiteral("[blit] offscreen=(%1,%2,%3,%4) widgetFbo=(%5,%6,%7,%8) fbo=%9")
                      .arg(int(src[0])).arg(int(src[1])).arg(int(src[2])).arg(int(src[3]))
                      .arg(int(dst[0])).arg(int(dst[1])).arg(int(dst[2])).arg(int(dst[3]))
                      .arg(int(defaultFramebufferObject())));
        }
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
    GLsizei vw = 0, vh = 0;
    if (!queryWidgetFboPhysSize(this, dfo, vw, vh) || vw <= 0 || vh <= 0) {
        const qreal dpr2 = devicePixelRatioF();
        vw = qRound(width()*dpr2);
        vh = qRound(height()*dpr2);
    }
    glViewport(0, 0, vw, vh);
    glDisable(GL_SCISSOR_TEST);
    glScissor(0, 0, vw, vh);
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
