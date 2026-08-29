#version 330 core
in vec2 texCoord;
out vec4 fragColor;
uniform sampler2D tex;
uniform vec2 dirStep;

void main(void) {
    vec4 sum = vec4(0.0);
    const int SAMPLES = 16;
    for (int i = 0; i < SAMPLES; i++) {
        float t = (float(i) / float(SAMPLES - 1)) - 0.5; // -0.5 .. 0.5
        sum += texture(tex, texCoord + dirStep * t);
    }
    fragColor = sum / float(SAMPLES);
}
