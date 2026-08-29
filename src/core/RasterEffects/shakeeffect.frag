#version 330 core
in vec2 texCoord;
out vec4 fragColor;
uniform sampler2D tex;
uniform vec2 offset;
uniform float rotAngle;

void main(void) {
    vec2 center = vec2(0.5, 0.5);
    vec2 uv = texCoord - center;
    float s = sin(rotAngle);
    float c = cos(rotAngle);
    mat2 rot = mat2(c, -s, s, c);
    uv = rot * uv + center - offset;
    fragColor = texture(tex, uv);
}
