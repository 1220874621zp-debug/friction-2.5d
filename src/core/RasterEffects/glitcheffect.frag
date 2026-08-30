#version 330 core
in vec2 texCoord;
out vec4 fragColor;
uniform sampler2D tex;
uniform float intensity;
uniform float timeSeed;
uniform float colorDrift;

float hash11(float p) {
    p = fract(p * 0.1031);
    p *= p + 33.33;
    p *= p + p;
    return fract(p);
}

void main(void) {
    if (intensity <= 0.0001) {
        fragColor = texture(tex, texCoord);
        return;
    }

    float block = floor(texCoord.y * 24.0 + timeSeed * 3.7);
    float noise = hash11(block);
    float offset = 0.0;
    if (noise > 0.55) {
        offset = (hash11(block + 1.23) - 0.5) * intensity * 0.08;
    }

    vec2 uv = vec2(clamp(texCoord.x + offset, 0.0, 1.0), texCoord.y);
    float drift = colorDrift * intensity * 0.015;

    float r = texture(tex, vec2(clamp(uv.x + drift, 0.0, 1.0), uv.y)).r;
    float g = texture(tex, uv).g;
    float b = texture(tex, vec2(clamp(uv.x - drift, 0.0, 1.0), uv.y)).b;
    float a = texture(tex, uv).a;

    fragColor = vec4(r, g, b, a);
}
