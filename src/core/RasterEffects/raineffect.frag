#version 330 core
in vec2 texCoord;
out vec4 fragColor;
uniform sampler2D tex;
uniform float density;
uniform float timeOffset;
uniform float slant;
uniform float opacity;
uniform vec4 rainColor;

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float rainLayer(vec2 uv, float scale, float speed, float seed) {
    vec2 p = vec2(uv.x + uv.y * slant + seed, uv.y);
    p.x *= scale * 1.5;
    p.y = (p.y + timeOffset * speed) * scale * 0.1;

    vec2 id = floor(p);
    vec2 gv = fract(p) - 0.5;

    float n = hash12(id + seed * 17.13);
    if (n < 0.5) return 0.0;

    float dropX = (n - 0.5) * 1.6 - 0.8;
    float dist = abs(gv.x - dropX * 0.35);

    float streakW = 0.06;
    float streak = smoothstep(streakW, 0.0, dist) * smoothstep(0.5, -0.4, gv.y);
    return streak * (0.5 + 0.5 * n);
}

void main(void) {
    vec4 src = texture(tex, texCoord);
    float d = clamp(density, 5.0, 300.0);

    float r1 = rainLayer(texCoord, d * 0.5, 1.8, 1.0) * 0.7;
    float r2 = rainLayer(texCoord + vec2(0.23, 0.41), d * 1.0, 1.4, 2.5) * 0.5;
    float r3 = rainLayer(texCoord + vec2(0.67, 0.83), d * 2.0, 1.0, 5.7) * 0.3;

    float rain = clamp((r1 + r2 + r3) * opacity, 0.0, 1.0);
    vec3 rainRgb = rainColor.rgb;

    vec3 blendedRgb = src.rgb + rainRgb * rain * 1.5;
    float finalAlpha = clamp(src.a + rain * rainColor.a * opacity, 0.0, 1.0);

    fragColor = vec4(clamp(blendedRgb, 0.0, 1.0), finalAlpha);
}
