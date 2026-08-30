#version 330 core
in vec2 texCoord;
out vec4 fragColor;
uniform sampler2D tex;
uniform float amount;
uniform float frequency;
uniform float timeOffset;
uniform int dispType;
uniform float chromatic;

vec2 hash2(vec2 p) {
    p = vec2(dot(p, vec2(127.1, 311.7)), dot(p, vec2(269.5, 183.3)));
    return -1.0 + 2.0 * fract(sin(p) * 43758.5453123);
}

float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(dot(hash2(i + vec2(0.0,0.0)), f - vec2(0.0,0.0)),
                   dot(hash2(i + vec2(1.0,0.0)), f - vec2(1.0,0.0)), u.x),
               mix(dot(hash2(i + vec2(0.0,1.0)), f - vec2(0.0,1.0)),
                   dot(hash2(i + vec2(1.0,1.0)), f - vec2(1.0,1.0)), u.x), u.y);
}

vec2 getDisplacement(vec2 uv) {
    float f = max(frequency, 0.1);
    float t = timeOffset;
    vec2 d = vec2(0.0);

    if (dispType == 0) { // Water wave
        d.x = sin(uv.y * f * 20.0 + t * 5.0) * 0.5 + cos(uv.x * f * 15.0 - t * 3.0) * 0.5;
        d.y = cos(uv.x * f * 20.0 + t * 5.0) * 0.5 + sin(uv.y * f * 15.0 - t * 3.0) * 0.5;
    } else if (dispType == 1) { // Heat wave / Turbulent
        float n1 = noise(uv * f * 10.0 + vec2(0.0, t * 2.0));
        float n2 = noise(uv * f * 10.0 + vec2(t * 2.0, 0.0) + vec2(5.2, 1.3));
        d = vec2(n1, n2);
    } else { // Glass refraction
        vec2 center = uv - vec2(0.5);
        float r = length(center);
        float angle = atan(center.y, center.x);
        d.x = sin(r * f * 30.0 - t * 4.0) * cos(angle);
        d.y = sin(r * f * 30.0 - t * 4.0) * sin(angle);
    }
    return d * (amount * 0.002);
}

void main(void) {
    vec2 disp = getDisplacement(texCoord);
    float chr = chromatic * 0.001;

    vec4 colR = texture(tex, clamp(texCoord + disp + vec2(chr, 0.0), 0.0, 1.0));
    vec4 colG = texture(tex, clamp(texCoord + disp, 0.0, 1.0));
    vec4 colB = texture(tex, clamp(texCoord + disp - vec2(chr, 0.0), 0.0, 1.0));

    fragColor = vec4(colR.r, colG.g, colB.b, colG.a);
}
