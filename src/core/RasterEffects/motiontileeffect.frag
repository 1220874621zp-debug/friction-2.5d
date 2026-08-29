#version 330 core
in vec2 texCoord;
out vec4 fragColor;
uniform sampler2D tex;
uniform float tileCountX;
uniform float tileCountY;
uniform vec2 offset;
uniform int mirrorEdges;

void main(void) {
    vec2 p = (texCoord - offset) * vec2(tileCountX, tileCountY);
    vec2 cell = floor(p);
    vec2 f = fract(p);

    if (mirrorEdges != 0) {
        if (mod(cell.x, 2.0) >= 1.0) f.x = 1.0 - f.x;
        if (mod(cell.y, 2.0) >= 1.0) f.y = 1.0 - f.y;
    }

    fragColor = texture(tex, f);
}
