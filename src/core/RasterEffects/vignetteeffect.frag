#version 330 core
in vec2 texCoord;
out vec4 fragColor;
uniform sampler2D tex;
uniform float radius;
uniform float feather;
uniform float opacity;
uniform vec4 color;

void main(void) {
    vec4 src = texture(tex, texCoord);
    vec2 center = vec2(0.5, 0.5);
    float dist = distance(texCoord, center) * 1.4142; // diagonal normalized to ~1.0
    float start = max(0.0, radius - feather);
    float end = max(start + 0.0001, radius + feather);
    float vig = smoothstep(start, end, dist) * opacity;
    vec3 mixed = mix(src.rgb, color.rgb * src.a, vig);
    fragColor = vec4(mixed, src.a);
}
