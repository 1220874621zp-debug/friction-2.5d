#version 330 core
in vec2 texCoord;
out vec4 fragColor;
uniform sampler2D tex;
uniform float amount;
uniform float size;
uniform float seed;
uniform float colorGrain;

float hash(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

void main(void) {
    vec4 src = texture(tex, texCoord);
    if (src.a < 0.001) {
        fragColor = vec4(0.0);
        return;
    }

    vec2 uv = texCoord / max(size * 0.005, 0.0001);
    float nR = hash(uv + vec2(seed * 17.13, seed * 31.41)) * 2.0 - 1.0;
    float nG = (colorGrain > 0.5) ? (hash(uv + vec2(seed * 43.17, seed * 67.89)) * 2.0 - 1.0) : nR;
    float nB = (colorGrain > 0.5) ? (hash(uv + vec2(seed * 89.23, seed * 11.57)) * 2.0 - 1.0) : nR;

    float luma = dot(src.rgb, vec3(0.299, 0.587, 0.114));
    // Film grain response curve: mid-tones receive the most grain
    float grainMask = 1.0 - 2.0 * abs(luma - 0.5);
    grainMask = mix(0.4, 1.0, grainMask);

    vec3 grain = vec3(nR, nG, nB) * (amount * 0.01) * grainMask;
    vec3 outRgb = src.rgb + grain;

    fragColor = vec4(clamp(outRgb, 0.0, 1.0), src.a);
}
