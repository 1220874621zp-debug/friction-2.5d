#version 330 core
in vec2 texCoord;
out vec4 fragColor;
uniform sampler2D tex;
uniform float angle;
uniform vec2 center;

void main(void) {
    vec4 sum = vec4(0.0);
    vec2 toCenter = texCoord - center;
    float dist = length(toCenter);
    float baseAng = atan(toCenter.y, toCenter.x);
    const int SAMPLES = 32;
    float dither = fract(sin(dot(texCoord, vec2(12.9898, 78.233))) * 43758.5453);

    for (int i = 0; i < SAMPLES; i++) {
        float t = ((float(i) + dither) / float(SAMPLES)) - 0.5;
        float a = baseAng + angle * t;
        vec2 uv = center + vec2(cos(a), sin(a)) * dist;
        sum += texture(tex, uv);
    }
    fragColor = sum / float(SAMPLES);
}
