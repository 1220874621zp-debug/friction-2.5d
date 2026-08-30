#version 330 core
in vec2 texCoord;
out vec4 fragColor;
uniform sampler2D tex;
uniform vec2 center;
uniform float mirrorX;
uniform float mirrorY;

void main(void) {
    vec2 uv = texCoord;
    if (mirrorX > 0.5) {
        uv.x = center.x - abs(uv.x - center.x);
    }
    if (mirrorY > 0.5) {
        uv.y = center.y - abs(uv.y - center.y);
    }
    fragColor = texture(tex, uv);
}
