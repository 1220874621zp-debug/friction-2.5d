#version 330 core
in vec2 texCoord;
out vec4 fragColor;
uniform sampler2D tex;
uniform float levels;

void main(void) {
    vec4 src = texture(tex, texCoord);
    float n = max(2.0, levels);
    vec3 c = floor(src.rgb * (n - 1.0) + 0.5) / (n - 1.0);
    fragColor = vec4(c, src.a);
}
