#version 330 core
in vec2 texCoord;
out vec4 fragColor;
uniform sampler2D tex;
uniform int noiseType;
uniform float scale;
uniform float complexity;
uniform float evolution;
uniform float brightness;
uniform float contrast;
uniform float opacity;
uniform float invert;
uniform vec4 color1;
uniform vec4 color2;

// 3D Simplex & Value noise
vec4 mod289(vec4 x) { return x - floor(x * (1.0 / 289.0)) * 289.0; }
vec4 permute(vec4 x) { return mod289(((x*34.0)+1.0)*x); }
vec4 taylorInvSqrt(vec4 r) { return 1.79284291400159 - 0.85373472095314 * r; }

float snoise(vec3 v) {
    const vec2 C = vec2(1.0/6.0, 1.0/3.0);
    const vec4 D = vec4(0.0, 0.5, 1.0, 2.0);

    vec3 i  = floor(v + dot(v, C.yyy));
    vec3 x0 = v - i + dot(i, C.xxx);

    vec3 g = step(x0.yzx, x0.xyz);
    vec3 l = 1.0 - g;
    vec3 i1 = min(g.xyz, l.zxy);
    vec3 i2 = max(g.xyz, l.zxy);

    vec3 x1 = x0 - i1 + C.xxx;
    vec3 x2 = x0 - i2 + C.yyy;
    vec3 x3 = x0 - D.yyy;

    i = mod289(vec4(i, 0.0)).xyz;
    vec4 p = permute(permute(permute(
                i.z + vec4(0.0, i1.z, i2.z, 1.0))
              + i.y + vec4(0.0, i1.y, i2.y, 1.0))
              + i.x + vec4(0.0, i1.x, i2.x, 1.0));

    float n_ = 0.142857142857;
    vec3 ns = n_ * D.wyz - D.xzx;

    vec4 j = p - 49.0 * floor(p * ns.z * ns.z);

    vec4 x_ = floor(j * ns.z);
    vec4 y_ = floor(j - 7.0 * x_);

    vec4 x = x_ *ns.x + ns.yyyy;
    vec4 y = y_ *ns.x + ns.yyyy;
    vec4 h = 1.0 - abs(x) - abs(y);

    vec4 b0 = vec4(x.xy, y.xy);
    vec4 b1 = vec4(x.zw, y.zw);

    vec4 s0 = floor(b0)*2.0 + 1.0;
    vec4 s1 = floor(b1)*2.0 + 1.0;
    vec4 sh = -step(h, vec4(0.0));

    vec4 a0 = b0.xzyw + s0.xzyw*sh.xxyy;
    vec4 a1 = b1.xzyw + s1.xzyw*sh.zzww;

    vec3 p0 = vec3(a0.xy, h.x);
    vec3 p1 = vec3(a0.zw, h.y);
    vec3 p2 = vec3(a1.xy, h.z);
    vec3 p3 = vec3(a1.zw, h.w);

    vec4 norm = taylorInvSqrt(vec4(dot(p0,p0), dot(p1,p1), dot(p2, p2), dot(p3,p3)));
    p0 *= norm.x; p1 *= norm.y; p2 *= norm.z; p3 *= norm.w;

    vec4 m = max(0.6 - vec4(dot(x0,x0), dot(x1,x1), dot(x2,x2), dot(x3,x3)), 0.0);
    m = m * m;
    return 42.0 * dot(m*m, vec4(dot(p0,x0), dot(p1,x1), dot(p2,x2), dot(p3,x3)));
}

// Worley / Cellular
float worley(vec3 p) {
    vec3 id = floor(p);
    vec3 fd = fract(p);
    float minDist = 1.0;
    for (int x = -1; x <= 1; x++) {
        for (int y = -1; y <= 1; y++) {
            for (int z = -1; z <= 1; z++) {
                vec3 g = vec3(float(x), float(y), float(z));
                vec3 o = fract(sin(vec3(dot(id + g, vec3(12.9898, 78.233, 45.164)),
                                        dot(id + g, vec3(39.346, 11.135, 83.155)),
                                        dot(id + g, vec3(73.156, 52.235, 9.151)))) * 43758.5453);
                vec3 r = g + o - fd;
                float d = dot(r, r);
                minDist = min(minDist, d);
            }
        }
    }
    return clamp(sqrt(minDist), 0.0, 1.0);
}

float fbm(vec3 p, int octaves, int mode) {
    float v = 0.0;
    float a = 0.5;
    vec3 shift = vec3(100.0);
    for (int i = 0; i < 8; ++i) {
        if (i >= octaves) break;
        float n = 0.0;
        if (mode == 0) {
            n = snoise(p) * 0.5 + 0.5;
        } else if (mode == 1) {
            n = abs(snoise(p));
        } else if (mode == 2) {
            n = worley(p);
        } else {
            n = 1.0 - abs(snoise(p));
            n = n * n;
        }
        v += a * n;
        p = p * 2.0 + shift;
        a *= 0.5;
    }
    return v;
}

void main(void) {
    vec4 src = texture(tex, texCoord);
    float sc = max(scale, 1.0) * 0.05;
    vec3 p = vec3(texCoord * sc, evolution * 0.01);

    int octaves = int(clamp(complexity, 1.0, 8.0));
    float rawNoise = fbm(p, octaves, noiseType);

    if (invert > 0.5) rawNoise = 1.0 - rawNoise;

    float val = (rawNoise - 0.5) * (contrast * 0.01) + 0.5 + (brightness * 0.01);
    val = clamp(val, 0.0, 1.0);

    vec4 noiseCol = mix(color1, color2, val);

    vec3 outRgb;
    if (src.a > 0.01) {
        outRgb = mix(src.rgb, noiseCol.rgb, opacity * noiseCol.a);
    } else {
        outRgb = noiseCol.rgb;
    }

    float outA = clamp(src.a + opacity * noiseCol.a, 0.0, 1.0);
    fragColor = vec4(clamp(outRgb, 0.0, 1.0), outA);
}
