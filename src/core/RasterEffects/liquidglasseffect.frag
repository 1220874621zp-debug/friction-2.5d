#version 330 core
in vec2 texCoord;
out vec4 fragColor;
uniform sampler2D tex;
uniform float blurRadius;
uniform float refraction;
uniform float surfaceNoise;
uniform float thickness;
uniform float highlightIntensity;
uniform float lightAngle;
uniform float highlightSize;
uniform float edgeSoftness;
uniform float magnification;
uniform vec4 glassTint;
uniform float tintOpacity;

float hash(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

float smoothNoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash(i);
    float b = hash(i + vec2(1.0, 0.0));
    float c = hash(i + vec2(0.0, 1.0));
    float d = hash(i + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

void main(void) {
    vec4 src = texture(tex, texCoord);
    if (src.a < 0.001) {
        fragColor = vec4(0.0);
        return;
    }

    vec2 center = vec2(0.5, 0.5);
    vec2 offset = texCoord - center;

    // Magnification & lens bulge
    float mag = max(magnification, 0.1);
    vec2 uvLens = center + offset / mag;

    // Surface wave & noise normal distortion
    float n1 = smoothNoise(texCoord * 15.0);
    float n2 = smoothNoise(texCoord * 15.0 + vec2(7.3, 11.9));
    vec2 noiseNorm = vec2(n1 - 0.5, n2 - 0.5) * (surfaceNoise * 0.01);

    // Refractive displacement
    vec2 refrOffset = (offset * (thickness * 0.02) + noiseNorm) * (refraction * 0.01);
    vec2 sampleUV = clamp(uvLens + refrOffset, 0.0, 1.0);

    // Multi-sample blur diffusion for frosted glass look
    vec4 blurredCol = vec4(0.0);
    float blur = blurRadius * 0.002;
    int samples = 9;
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            vec2 bUV = clamp(sampleUV + vec2(float(x), float(y)) * blur, 0.0, 1.0);
            blurredCol += texture(tex, bUV);
        }
    }
    blurredCol /= 9.0;

    // Specular highlight
    float rad = radians(lightAngle);
    vec2 lightDir = vec2(cos(rad), sin(rad));
    vec2 surfaceGrad = normalize(offset + noiseNorm * 5.0 + vec2(0.001));
    float spec = max(0.0, dot(surfaceGrad, lightDir));
    float hlSize = max(highlightSize * 0.5, 1.0);
    float highlight = pow(spec, hlSize) * (highlightIntensity * 0.01);

    // Glass tint & opacity
    vec3 tintedRgb = mix(blurredCol.rgb, glassTint.rgb, tintOpacity * 0.01 * glassTint.a);
    vec3 finalRgb = tintedRgb + vec3(highlight);

    fragColor = vec4(clamp(finalRgb, 0.0, 1.0), src.a);
}
