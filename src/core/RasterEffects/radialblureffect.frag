#version 330 core
in vec2 texCoord;
out vec4 fragColor;
uniform sampler2D tex;
uniform float amount;
uniform vec2 center;

void main(void) {
    vec4 sum = vec4(0.0);
    vec2 dir = texCoord - center;
    const int SAMPLES = 16;
    for (int i = 0; i < SAMPLES; i++) {
        float scale = 1.0 - amount * (float(i) / float(SAMPLES - 1));
        vec2 uv = center + dir * scale;
        sum += texture(tex, uv);
    }
    fragColor = sum / float(SAMPLES);
}
