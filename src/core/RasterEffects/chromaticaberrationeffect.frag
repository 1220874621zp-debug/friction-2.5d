#version 330 core
in vec2 texCoord;
out vec4 fragColor;
uniform sampler2D tex;
uniform vec2 offset;

void main(void) {
    vec4 colG = texture(tex, texCoord);
    vec4 colR = texture(tex, texCoord - offset);
    vec4 colB = texture(tex, texCoord + offset);
    float a = max(colG.a, max(colR.a, colB.a));
    fragColor = vec4(colR.r, colG.g, colB.b, a);
}
