#version 330 core
in vec2 texCoord;
out vec4 fragColor;
uniform sampler2D tex;
uniform float amount;
uniform vec2 center;

void main(void) {
    vec4 sum = vec4(0.0);
    vec2 toCenter = center - texCoord;
    const int SAMPLES = 32;
    float dither = fract(sin(dot(texCoord, vec2(12.9898, 78.233))) * 43758.5453);

    for (int i = 0; i < SAMPLES; i++) {
        float scale = 1.0 + amount * ((float(i) + dither) / float(SAMPLES));
        vec2 uv = center - toCenter * scale;
        sum += texture(tex, uv);
    }
    fragColor = sum / float(SAMPLES);
}
