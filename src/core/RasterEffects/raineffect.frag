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

float rainLayer(vec2 uv, float scale, float speedMult) {
    vec2 p = uv * scale;
    p.y += timeOffset * speedMult;
    p.x += p.y * slant;

    vec2 id = floor(p);
    vec2 gv = fract(p) - 0.5;

    float n = hash(id);
    if (n < 0.75) return 0.0;

    float lineX = (n - 0.75) * 4.0 - 0.5;
    float d = abs(gv.x - lineX * 0.4);
    float streak = smoothstep(0.06, 0.0, d) * smoothstep(0.5, 0.0, abs(gv.y));
    return streak * (n * 0.6 + 0.4);
}

void main(void) {
    vec4 src = texture(tex, texCoord);
    float rain = 0.0;
    rain += rainLayer(texCoord, density * 0.8, 1.0) * 0.6;
    rain += rainLayer(texCoord, density * 1.4, 1.6) * 0.4;
    rain = clamp(rain * opacity, 0.0, 1.0);

    vec3 mixed = mix(src.rgb, rainColor.rgb * max(0.5, src.a), rain);
    fragColor = vec4(mixed, src.a);
}
