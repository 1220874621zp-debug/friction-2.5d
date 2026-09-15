#ifndef GLCALLWRAP_H
#define GLCALLWRAP_H

#include "skia/skiaincludes.h"

// Wraps the GL interface's draw-path entry points so the exact call that
// raises a GL error is logged by name ([glcall] DrawElements err=1282 ...).
// Returns the original interface unchanged when wrapping is disabled via
// FRICTION_GL_WRAP_OFF=1. No-op design goal: pass-through otherwise.
sk_sp<const GrGLInterface> frictionWrapGlInterfaceForDiagnostics(
        const sk_sp<const GrGLInterface>& iface);

#endif // GLCALLWRAP_H
