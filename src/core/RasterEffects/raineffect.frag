#version 330 core
in vec2 texCoord;
out vec4 fragColor;
uniform sampler2D tex;
uniform float density;
uniform float timeOffset;
uniform float slant;
uniform float opacity;
uniform vec4 rainColor;

float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}

float rainStreak(vec2 uv, float scale, float speed) {
    vec2 p = vec2(uv.x + uv.y * slant, uv.y);
    p.x *= scale * 2.0;
    p.y = (p.y + timeOffset * speed) * scale * 0.2;

    vec2 id = floor(p);
    vec2 gv = fract(p) - 0.5;

    float n = hash(id);
    if (n < 0.65) return 0.0;

    float lineX = (n - 0.65) / 0.35 - 0.5;
    float dist = abs(gv.x - lineX * 0.4);
    float streak = smoothstep(0.1, 0.0, dist) * smoothstep(0.5, 0.0, abs(gv.y));
    return streak * (n * 0.5 + 0.5);
}

void main(void) {
    vec4 src = texture(tex, texCoord);
    float d = max(10.0, density);
    
    float r1 = rainStreak(texCoord, d * 0.6, 1.2) * 0.4;
    float r2 = rainStreak(texCoord + vec2(0.33, 0.17), d * 1.0, 1.6) * 0.5;
    float r3 = rainStreak(texCoord + vec2(0.67, 0.53), d * 1.5, 2.0) * 0.3;

    float rain = clamp((r1 + r2 + r3) * opacity, 0.0, 1.0);
    vec3 result = src.rgb + rainColor.rgb * rain * max(0.2, src.a);
    fragColor = vec4(clamp(result, 0.0, 1.0), src.a);
}
