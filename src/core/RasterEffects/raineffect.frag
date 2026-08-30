#version 330 core
in vec2 texCoord;
out vec4 fragColor;
uniform sampler2D tex;
uniform float density;
uniform float timeOffset;
uniform float slant;
uniform float opacity;
uniform vec4 rainColor;

vec2 hash22(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.xx + p3.yz) * p3.zy);
}

float rainLayer(vec2 uv, float scale, float speedMultiplier, float seed) {
    vec2 p = vec2(uv.x + (1.0 - uv.y) * slant + seed * 19.17, uv.y);
    p.x *= scale;
    float yTime = timeOffset * speedMultiplier * 5.0;
    p.y = (p.y - yTime) * (scale * 0.12);

    vec2 id = floor(p);
    vec2 gv = fract(p) - 0.5;

    vec2 h = hash22(id + seed * 37.19);
    if (h.x < 0.45) return 0.0;

    float dropX = (h.y - 0.5) * 0.8;
    float dist = abs(gv.x - dropX);

    float streakW = 0.08;
    float horizontalFalloff = smoothstep(streakW, 0.0, dist);
    float verticalFalloff = smoothstep(0.4, -0.45, gv.y);

    return horizontalFalloff * verticalFalloff * (0.6 + 0.4 * h.x);
}

void main(void) {
    vec4 src = texture(tex, texCoord);
    float d = clamp(density, 10.0, 300.0);

    float r1 = rainLayer(texCoord, d * 0.7, 1.8, 1.0) * 0.8;
    float r2 = rainLayer(texCoord + vec2(0.31, 0.47), d * 1.3, 1.4, 3.7) * 0.5;
    float r3 = rainLayer(texCoord + vec2(0.73, 0.89), d * 2.2, 1.0, 7.3) * 0.3;

    float rainIntensity = clamp((r1 + r2 + r3) * opacity, 0.0, 1.0);
    vec3 rainRgb = rainColor.rgb;

    vec3 outRgb;
    if (src.a > 0.01) {
        outRgb = mix(src.rgb, rainRgb, rainIntensity * rainColor.a);
        outRgb += rainRgb * rainIntensity * 0.5;
    } else {
        outRgb = rainRgb;
    }

    float outAlpha = clamp(src.a + rainIntensity * rainColor.a, 0.0, 1.0);
    fragColor = vec4(clamp(outRgb, 0.0, 1.0), outAlpha);
}
