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

void main(void) {
    vec4 src = texture(tex, texCoord);
    if (src.a < 0.001) {
        fragColor = vec4(0.0);
        return;
    }

    vec2 center = vec2(0.5, 0.5);
    vec2 offset = texCoord - center;

    // Magnification (subtle lens zoom)
    float mag = max(magnification, 0.1);
    vec2 uvLens = center + offset / mag;

    // Liquid surface wave normal distortion (subtle harmonic waves)
    float freq = 8.0 + surfaceNoise * 0.15;
    vec2 waveNorm = vec2(
        sin(texCoord.y * freq + texCoord.x * 4.0),
        cos(texCoord.x * freq - texCoord.y * 4.0)
    ) * (surfaceNoise * 0.0015);

    // Subtle lens curvature refraction
    vec2 lensCurv = offset * (thickness * 0.001);
    vec2 totalDisp = (lensCurv + waveNorm) * (refraction * 0.05);
    vec2 sampleUV = clamp(uvLens + totalDisp, 0.0, 1.0);

    // 25-tap frosted glass diffusion blur
    vec4 blurredCol = vec4(0.0);
    float bRadius = max(blurRadius, 0.0) * 0.0008;
    float totalWeight = 0.0;
    for (int x = -2; x <= 2; ++x) {
        for (int y = -2; y <= 2; ++y) {
            float w = 1.0 / (1.0 + float(x*x + y*y) * 0.5);
            vec2 bUV = clamp(sampleUV + vec2(float(x), float(y)) * bRadius, 0.0, 1.0);
            blurredCol += texture(tex, bUV) * w;
            totalWeight += w;
        }
    }
    blurredCol /= totalWeight;

    // Specular highlight on wave ridges & glass surface
    float rad = radians(lightAngle);
    vec2 lightDir = vec2(cos(rad), sin(rad));
    vec2 surfaceGrad = normalize(waveNorm * 100.0 + lensCurv * 50.0 + vec2(0.0001));
    float waveDot = max(0.0, dot(surfaceGrad, lightDir));
    float hlPow = max(highlightSize * 0.5, 2.0);
    float highlight = pow(waveDot, hlPow) * (highlightIntensity * 0.005);

    // Glass tinting & opacity
    vec3 tinted = mix(blurredCol.rgb, glassTint.rgb, (tintOpacity * 0.01) * glassTint.a);
    vec3 finalRgb = tinted + vec3(highlight);

    fragColor = vec4(clamp(finalRgb, 0.0, 1.0), src.a * blurredCol.a);
}
