#version 330 core
in vec2 texCoord;
out vec4 fragColor;
uniform sampler2D tex;
uniform float barSize;
uniform float feather;
uniform vec4 color;

void main(void) {
    vec4 src = texture(tex, texCoord);
    float dMin = min(texCoord.y, 1.0 - texCoord.y);
    float end = barSize + max(0.0001, feather);
    float barMask = 1.0 - smoothstep(barSize, end, dMin);
    vec3 mixed = mix(src.rgb, color.rgb * src.a, barMask);
    fragColor = vec4(mixed, src.a);
}
