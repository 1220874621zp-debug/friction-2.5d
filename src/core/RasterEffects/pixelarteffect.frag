#version 330 core
in vec2 texCoord;
out vec4 fragColor;
uniform sampler2D tex;
uniform float pixelSize;
uniform float paletteSize;
uniform float dither;
uniform float edgeSharpness;
uniform float saturation;
uniform float scanline;
uniform float chromatic;

// 4x4 Bayer Matrix
const float bayer4[16] = float[16](
     0.0/16.0,  8.0/16.0,  2.0/16.0, 10.0/16.0,
    12.0/16.0,  4.0/16.0, 14.0/16.0,  6.0/16.0,
     3.0/16.0, 11.0/16.0,  1.0/16.0,  9.0/16.0,
    15.0/16.0,  7.0/16.0, 13.0/16.0,  5.0/16.0
);

vec3 rgb2hsv(vec3 c) {
    vec4 K = vec4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
    vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));
    vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));
    float d = q.x - min(q.w, q.y);
    float e = 1.0e-10;
    return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}

vec3 hsv2rgb(vec3 c) {
    vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

void main(void) {
    vec2 texSize = vec2(textureSize(tex, 0));
    float pSize = max(pixelSize, 1.0);

    // Pixel block quantization
    vec2 numBlocks = max(texSize / pSize, vec2(1.0));
    vec2 blockUV = (floor(texCoord * numBlocks) + 0.5) / numBlocks;

    // Chromatic offset on pixel sampling
    float chr = chromatic * 0.001;
    float r = texture(tex, clamp(blockUV + vec2(chr, 0.0), 0.0, 1.0)).r;
    float g = texture(tex, clamp(blockUV, 0.0, 1.0)).g;
    float b = texture(tex, clamp(blockUV - vec2(chr, 0.0), 0.0, 1.0)).b;
    float a = texture(tex, blockUV).a;

    if (a < 0.001) {
        fragColor = vec4(0.0);
        return;
    }

    vec3 col = vec3(r, g, b);

    // Saturation boost
    if (abs(saturation - 100.0) > 0.1) {
        vec3 hsv = rgb2hsv(col);
        hsv.y = clamp(hsv.y * (saturation * 0.01), 0.0, 1.0);
        col = hsv2rgb(hsv);
    }

    // Bayer dithering
    if (dither > 0.01) {
        ivec2 pixelPos = ivec2(mod(gl_FragCoord.xy, 4.0));
        float dVal = bayer4[pixelPos.y * 4 + pixelPos.x] - 0.5;
        col += vec3(dVal * (dither * 0.01));
    }

    // Color palette quantization
    float levels = max(paletteSize, 2.0);
    col = floor(col * (levels - 1.0) + 0.5) / (levels - 1.0);

    // CRT scanlines
    if (scanline > 0.01) {
        float sl = sin(gl_FragCoord.y * 1.5) * 0.5 + 0.5;
        col *= (1.0 - sl * (scanline * 0.005));
    }

    // Pixel grid outline / edge sharpness
    if (edgeSharpness > 0.01) {
        vec2 gridFrac = abs(fract(texCoord * numBlocks) - 0.5);
        float edgeDist = max(gridFrac.x, gridFrac.y);
        float gridLine = smoothstep(0.46, 0.5, edgeDist) * (edgeSharpness * 0.008);
        col *= (1.0 - gridLine);
    }

    fragColor = vec4(clamp(col, 0.0, 1.0), a);
}
