#version 330 core
in vec2 texCoord;
out vec4 fragColor;
uniform sampler2D tex;
uniform vec3 lut[66];

// composite curve table (master applied first, then per-channel),
// 66 samples linearly interpolated - mirrors the CPU 256-entry LUT
vec3 curveMap(const vec3 c) {
    // NOTE: no `const` on locals initialized from the parameter - a
    // const initializer must be a constant expression in GLSL 330 and
    // strict compilers (glslang-based) reject `const vec3 t = clamp(...)`
    vec3 t = clamp(c, 0.0, 1.0) * 65.0;
    ivec3 i = ivec3(min(t, vec3(64.0)));
    vec3 a = vec3(lut[i.r].r, lut[i.g].g, lut[i.b].b);
    vec3 b = vec3(lut[i.r + 1].r, lut[i.g + 1].g, lut[i.b + 1].b);
    return mix(a, b, t - vec3(i));
}

void main(void) {
    // NOTE: NOT `const` - a const initializer must be a constant expression
    // in GLSL 330 and a texture sample is not one; strict drivers rejected
    // the whole program (the effect then silently vanished on the canvas)
    vec4 src = texture(tex, texCoord);
    fragColor = vec4(clamp(curveMap(src.rgb), 0.0, 1.0), src.a);
}
