#version 330 core
in vec2 texCoord;
out vec4 fragColor;
uniform sampler2D tex;
uniform vec2 dirStep;

void main(void) {
    vec4 sum = vec4(0.0);
    const int SAMPLES = 32;
    float dither = fract(sin(dot(texCoord, vec2(12.9898, 78.233))) * 43758.5453);

    for (int i = 0; i < SAMPLES; i++) {
        float t = ((float(i) + dither) / float(SAMPLES)) - 0.5;
        sum += texture(tex, texCoord + dirStep * t);
    }
    fragColor = sum / float(SAMPLES);
}
