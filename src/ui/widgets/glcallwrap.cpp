#include "glcallwrap.h"

#include <QDebug>
#include <QFile>
#include <QDateTime>
#include <QMutex>

// Per-call GL error attribution.
// The intel driver only reports "GL_INVALID_OPERATION ... Generic error"
// through KHR_debug, never naming the call. Wrapping the interface's
// function pointers pins the error to the exact entry point.

namespace {

// This Skia version nests the function table inside GrGLInterface
// (GrGLInterface::Functions, GrGLInterface.h) - there is no standalone
// GrGLFunctions type.
GrGLInterface::Functions gOrig;
int gLogBudget = 80;
QMutex gLogMutex;

void reportErr(const char* const name) {
    if (!gOrig.fGetError) { return; }
    const GrGLenum err = gOrig.fGetError();
    if (!err || gLogBudget <= 0) { return; }
    --gLogBudget;
    qWarning() << "[glcall]" << name << "err" << int(err);
    QMutexLocker lock(&gLogMutex);
    QFile f(QStringLiteral("gl_diag.log"));
    if (f.open(QIODevice::WriteOnly | QIODevice::Append)) {
        f.write((QDateTime::currentDateTime().toString(Qt::ISODateWithMs) +
                 QStringLiteral(" [glcall] ") + name +
                 QStringLiteral(" err=") + QString::number(int(err)) +
                 QStringLiteral("\n")).toUtf8());
    }
}

} // namespace

#define FR_WRAP(name, sig, args)                                          \
    static GrGLvoid GR_GL_FUNCTION_TYPE frwrap_##name sig {               \
        gOrig.f##name args;                                               \
        reportErr(#name);                                                 \
    }
#define FR_WRAP_RET(name, ret, sig, args)                                 \
    static ret GR_GL_FUNCTION_TYPE frwrap_##name sig {                    \
        ret r = gOrig.f##name args;                                       \
        reportErr(#name);                                                 \
        return r;                                                         \
    }

FR_WRAP(ActiveTexture, (GrGLenum texture), (texture))
FR_WRAP(BindBuffer, (GrGLenum target, GrGLuint buffer), (target, buffer))
FR_WRAP(BindFramebuffer, (GrGLenum target, GrGLuint fbo), (target, fbo))
FR_WRAP(BindRenderbuffer, (GrGLenum t, GrGLuint r), (t, r))
FR_WRAP(BindTexture, (GrGLenum target, GrGLuint tex), (target, tex))
FR_WRAP(BindVertexArray, (GrGLuint array), (array))
FR_WRAP(BlendFunc, (GrGLenum sf, GrGLenum df), (sf, df))
FR_WRAP(BlitFramebuffer,
        (GrGLint a0, GrGLint a1, GrGLint a2, GrGLint a3,
         GrGLint b0, GrGLint b1, GrGLint b2, GrGLint b3,
         GrGLbitfield mask, GrGLenum filter),
        (a0, a1, a2, a3, b0, b1, b2, b3, mask, filter))
FR_WRAP(BufferData,
        (GrGLenum target, GrGLsizeiptr size, const GrGLvoid* data,
         GrGLenum usage),
        (target, size, data, usage))
FR_WRAP(BufferSubData,
        (GrGLenum target, GrGLintptr offset, GrGLsizeiptr size,
         const GrGLvoid* data),
        (target, offset, size, data))
FR_WRAP(Clear, (GrGLbitfield mask), (mask))
FR_WRAP(ClearColor, (GrGLfloat r, GrGLfloat g, GrGLfloat b, GrGLfloat a),
        (r, g, b, a))
FR_WRAP(ClearStencil, (GrGLint s), (s))
FR_WRAP(ColorMask, (GrGLboolean r, GrGLboolean g, GrGLboolean b,
                    GrGLboolean a), (r, g, b, a))
FR_WRAP(CopyTexSubImage2D,
        (GrGLenum t, GrGLint l, GrGLint xo, GrGLint yo, GrGLint x,
         GrGLint y, GrGLsizei w, GrGLsizei h),
        (t, l, xo, yo, x, y, w, h))
FR_WRAP(DeleteBuffers, (GrGLsizei n, const GrGLuint* b), (n, b))
FR_WRAP(DeleteFramebuffers, (GrGLsizei n, const GrGLuint* f), (n, f))
FR_WRAP(DeleteSync, (GrGLsync s), (s))
FR_WRAP(DeleteTextures, (GrGLsizei n, const GrGLuint* t), (n, t))
FR_WRAP(DepthMask, (GrGLboolean m), (m))
FR_WRAP(Disable, (GrGLenum cap), (cap))
FR_WRAP(DisableVertexAttribArray, (GrGLuint i), (i))
FR_WRAP(DiscardFramebuffer,
        (GrGLenum t, GrGLsizei n, const GrGLenum* a), (t, n, a))
FR_WRAP(DrawArrays, (GrGLenum m, GrGLint f, GrGLsizei c), (m, f, c))
FR_WRAP(DrawArraysInstanced,
        (GrGLenum m, GrGLint f, GrGLsizei c, GrGLsizei p), (m, f, c, p))
FR_WRAP(DrawBuffer, (GrGLenum b), (b))
FR_WRAP(DrawBuffers, (GrGLsizei n, const GrGLenum* b), (n, b))
FR_WRAP(DrawElements,
        (GrGLenum m, GrGLsizei c, GrGLenum t, const GrGLvoid* i),
        (m, c, t, i))
FR_WRAP(DrawElementsInstanced,
        (GrGLenum m, GrGLsizei c, GrGLenum t, const GrGLvoid* i,
         GrGLsizei p),
        (m, c, t, i, p))
FR_WRAP(Enable, (GrGLenum cap), (cap))
FR_WRAP(EnableVertexAttribArray, (GrGLuint i), (i))
FR_WRAP_RET(FenceSync, GrGLsync, (GrGLenum c, GrGLbitfield f), (c, f))
FR_WRAP(Finish, (), ())
FR_WRAP(Flush, (), ())
FR_WRAP(FramebufferRenderbuffer,
        (GrGLenum t, GrGLenum a, GrGLenum rt, GrGLuint r), (t, a, rt, r))
FR_WRAP(FramebufferTexture2D,
        (GrGLenum t, GrGLenum a, GrGLenum tt, GrGLuint tex, GrGLint l),
        (t, a, tt, tex, l))
FR_WRAP(GenBuffers, (GrGLsizei n, GrGLuint* b), (n, b))
FR_WRAP(GenFramebuffers, (GrGLsizei n, GrGLuint* f), (n, f))
FR_WRAP(GenTextures, (GrGLsizei n, GrGLuint* t), (n, t))
FR_WRAP(GenVertexArrays, (GrGLsizei n, GrGLuint* a), (n, a))
FR_WRAP(GenerateMipmap, (GrGLenum t), (t))
FR_WRAP(InvalidateFramebuffer,
        (GrGLenum t, GrGLsizei n, const GrGLenum* a), (t, n, a))
FR_WRAP(LineWidth, (GrGLfloat w), (w))
FR_WRAP_RET(MapBuffer, GrGLvoid*, (GrGLenum t, GrGLenum a), (t, a))
FR_WRAP_RET(MapBufferRange, GrGLvoid*,
            (GrGLenum t, GrGLintptr o, GrGLsizeiptr l, GrGLbitfield a),
            (t, o, l, a))
FR_WRAP(PixelStorei, (GrGLenum p, GrGLint v), (p, v))
FR_WRAP(ReadBuffer, (GrGLenum m), (m))
FR_WRAP(ReadPixels,
        (GrGLint x, GrGLint y, GrGLsizei w, GrGLsizei h, GrGLenum f,
         GrGLenum t, GrGLvoid* p),
        (x, y, w, h, f, t, p))
FR_WRAP(RenderbufferStorage,
        (GrGLenum t, GrGLenum i, GrGLsizei w, GrGLsizei h), (t, i, w, h))
FR_WRAP(Scissor, (GrGLint x, GrGLint y, GrGLsizei w, GrGLsizei h),
        (x, y, w, h))
FR_WRAP(StencilFunc, (GrGLint f, GrGLint r, GrGLuint m), (f, r, m))
FR_WRAP(StencilFuncSeparate,
        (GrGLenum face, GrGLint f, GrGLint r, GrGLuint m), (face, f, r, m))
FR_WRAP(StencilMask, (GrGLuint m), (m))
FR_WRAP(StencilOp, (GrGLint f, GrGLint zf, GrGLint zp), (f, zf, zp))
FR_WRAP(StencilOpSeparate,
        (GrGLenum face, GrGLint f, GrGLint zf, GrGLint zp),
        (face, f, zf, zp))
FR_WRAP(TexImage2D,
        (GrGLenum t, GrGLint l, GrGLint internal, GrGLsizei w, GrGLsizei h,
         GrGLint border, GrGLenum fmt, GrGLenum type, const GrGLvoid* p),
        (t, l, internal, w, h, border, fmt, type, p))
FR_WRAP(TexParameteri, (GrGLenum t, GrGLenum p, GrGLint v), (t, p, v))
FR_WRAP(TexStorage2D,
        (GrGLenum t, GrGLsizei lv, GrGLenum internal, GrGLsizei w,
         GrGLsizei h),
        (t, lv, internal, w, h))
FR_WRAP(TexSubImage2D,
        (GrGLenum t, GrGLint l, GrGLint xo, GrGLint yo, GrGLsizei w,
         GrGLsizei h, GrGLenum fmt, GrGLenum type, const GrGLvoid* p),
        (t, l, xo, yo, w, h, fmt, type, p))
FR_WRAP(Uniform1f, (GrGLint l, GrGLfloat v), (l, v))
FR_WRAP(Uniform1i, (GrGLint l, GrGLint v), (l, v))
FR_WRAP(Uniform2f, (GrGLint l, GrGLfloat x, GrGLfloat y), (l, x, y))
FR_WRAP(Uniform4fv, (GrGLint l, GrGLsizei c, const GrGLfloat* v), (l, c, v))
FR_WRAP(UniformMatrix4fv,
        (GrGLint l, GrGLsizei c, GrGLboolean t, const GrGLfloat* v),
        (l, c, t, v))
FR_WRAP_RET(UnmapBuffer, GrGLboolean, (GrGLenum t), (t))
FR_WRAP(UseProgram, (GrGLuint p), (p))
FR_WRAP(VertexAttribDivisor, (GrGLuint i, GrGLuint d), (i, d))
FR_WRAP(VertexAttribPointer,
        (GrGLuint i, GrGLint size, GrGLenum type, GrGLboolean norm,
         GrGLsizei stride, const GrGLvoid* ptr),
        (i, size, type, norm, stride, ptr))
FR_WRAP(Viewport, (GrGLint x, GrGLint y, GrGLsizei w, GrGLsizei h),
        (x, y, w, h))

#define FR_INSTALL(name) f.f##name = frwrap_##name

sk_sp<const GrGLInterface> frictionWrapGlInterfaceForDiagnostics(
        const sk_sp<const GrGLInterface>& iface) {
    // Off by default: with the per-frame resetContext fix every binding is
    // re-issued each frame, so the per-call glGetError overhead applies to
    // every single GL call. Set FRICTION_GL_WRAP_ON=1 to re-enable the
    // per-call error attribution ([glcall] lines + gl_diag.log).
    if (!iface || !qEnvironmentVariableIsSet("FRICTION_GL_WRAP_ON")) {
        return iface;
    }
    auto* copy = new GrGLInterface;
    copy->fStandard = iface->fStandard;
    copy->fExtensions = iface->fExtensions;
    copy->fFunctions = iface->fFunctions;
    gOrig = copy->fFunctions;
    auto& f = copy->fFunctions;
    FR_INSTALL(ActiveTexture);
    FR_INSTALL(BindBuffer);
    FR_INSTALL(BindFramebuffer);
    FR_INSTALL(BindRenderbuffer);
    FR_INSTALL(BindTexture);
    FR_INSTALL(BindVertexArray);
    FR_INSTALL(BlendFunc);
    FR_INSTALL(BlitFramebuffer);
    FR_INSTALL(BufferData);
    FR_INSTALL(BufferSubData);
    FR_INSTALL(Clear);
    FR_INSTALL(ClearColor);
    FR_INSTALL(ClearStencil);
    FR_INSTALL(ColorMask);
    FR_INSTALL(CopyTexSubImage2D);
    FR_INSTALL(DeleteBuffers);
    FR_INSTALL(DeleteFramebuffers);
    FR_INSTALL(DeleteSync);
    FR_INSTALL(DeleteTextures);
    FR_INSTALL(DepthMask);
    FR_INSTALL(Disable);
    FR_INSTALL(DisableVertexAttribArray);
    FR_INSTALL(DiscardFramebuffer);
    FR_INSTALL(DrawArrays);
    FR_INSTALL(DrawArraysInstanced);
    FR_INSTALL(DrawBuffer);
    FR_INSTALL(DrawBuffers);
    FR_INSTALL(DrawElements);
    FR_INSTALL(DrawElementsInstanced);
    FR_INSTALL(Enable);
    FR_INSTALL(EnableVertexAttribArray);
    FR_INSTALL(FenceSync);
    FR_INSTALL(Finish);
    FR_INSTALL(Flush);
    FR_INSTALL(FramebufferRenderbuffer);
    FR_INSTALL(FramebufferTexture2D);
    FR_INSTALL(GenBuffers);
    FR_INSTALL(GenFramebuffers);
    FR_INSTALL(GenTextures);
    FR_INSTALL(GenVertexArrays);
    FR_INSTALL(GenerateMipmap);
    FR_INSTALL(InvalidateFramebuffer);
    FR_INSTALL(LineWidth);
    FR_INSTALL(MapBuffer);
    FR_INSTALL(MapBufferRange);
    FR_INSTALL(PixelStorei);
    FR_INSTALL(ReadBuffer);
    FR_INSTALL(ReadPixels);
    FR_INSTALL(RenderbufferStorage);
    FR_INSTALL(Scissor);
    FR_INSTALL(StencilFunc);
    FR_INSTALL(StencilFuncSeparate);
    FR_INSTALL(StencilMask);
    FR_INSTALL(StencilOp);
    FR_INSTALL(StencilOpSeparate);
    FR_INSTALL(TexImage2D);
    FR_INSTALL(TexParameteri);
    FR_INSTALL(TexStorage2D);
    FR_INSTALL(TexSubImage2D);
    FR_INSTALL(Uniform1f);
    FR_INSTALL(Uniform1i);
    FR_INSTALL(Uniform2f);
    FR_INSTALL(Uniform4fv);
    FR_INSTALL(UniformMatrix4fv);
    FR_INSTALL(UnmapBuffer);
    FR_INSTALL(UseProgram);
    FR_INSTALL(VertexAttribDivisor);
    FR_INSTALL(VertexAttribPointer);
    FR_INSTALL(Viewport);
    qDebug() << "[glwin] GL call wrapper installed (per-call error attribution)";
    return sk_sp<const GrGLInterface>(copy);
}
