#version 330 core
in vec2 texCoord;
out vec4 fragColor;
uniform sampler2D tex;
uniform float density;
uniform float opacity;
uniform float timeOffset;

void main(void) {
    vec4 src = texture(tex, texCoord);
    float line = sin((texCoord.y + timeOffset) * density * 6.2831853);
    float scan = 0.5 + 0.5 * line;
    float factor = 1.0 - (scan * opacity);
    fragColor = vec4(src.rgb * factor, src.a);
}
