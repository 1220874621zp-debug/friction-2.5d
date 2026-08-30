#version 330 core
in vec2 texCoord;
out vec4 fragColor;
uniform sampler2D tex;
uniform vec2 shadowOffset;
uniform float softness;
uniform float opacity;
uniform vec4 shadowColor;

void main(void) {
    vec4 src = texture(tex, texCoord);
    
    // Sample shadow with Gaussian-like multi-tap blur around offset
    float shadowAlpha = 0.0;
    const int SAMPLES = 9;
    vec2 offsets[9] = vec2[](
        vec2( 0.0,  0.0),
        vec2( 1.0,  0.0), vec2(-1.0,  0.0), vec2( 0.0,  1.0), vec2( 0.0, -1.0),
        vec2( 0.7,  0.7), vec2(-0.7,  0.7), vec2( 0.7, -0.7), vec2(-0.7, -0.7)
    );
    float weights[9] = float[](
        0.28, 0.12, 0.12, 0.12, 0.12, 0.06, 0.06, 0.06, 0.06
    );

    vec2 sUv = texCoord - shadowOffset;
    for (int i = 0; i < SAMPLES; i++) {
        vec2 uv = sUv + offsets[i] * softness;
        shadowAlpha += texture(tex, uv).a * weights[i];
    }

    shadowAlpha = shadowAlpha * opacity * shadowColor.a;
    vec4 shadow = vec4(shadowColor.rgb, shadowAlpha);

    // Alpha composite: src over shadow
    vec3 outRgb = src.rgb * src.a + shadow.rgb * shadow.a * (1.0 - src.a);
    float outA = src.a + shadow.a * (1.0 - src.a);
    if (outA > 0.001) {
        outRgb /= outA;
    }
    fragColor = vec4(clamp(outRgb, 0.0, 1.0), clamp(outA, 0.0, 1.0));
}
