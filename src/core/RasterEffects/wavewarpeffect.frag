#version 330 core
in vec2 texCoord;
out vec4 fragColor;
uniform sampler2D tex;
uniform float amplitude;
uniform float frequency;
uniform float phase;
uniform vec2 waveDir;
uniform vec2 perpDir;

void main(void) {
    float pos = dot(texCoord, waveDir);
    float offset = sin(pos * frequency + phase) * amplitude;
    vec2 uv = texCoord + perpDir * offset;
    fragColor = texture(tex, uv);
}
