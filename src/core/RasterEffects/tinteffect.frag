#version 330 core
in vec2 texCoord;
out vec4 fragColor;
uniform sampler2D tex;
uniform vec4 mapBlack;
uniform vec4 mapWhite;
uniform float amount;

void main(void) {
    vec4 src = texture(tex, texCoord);
    float luma = dot(src.rgb, vec3(0.2126, 0.7152, 0.0722));
    vec3 tinted = mix(mapBlack.rgb, mapWhite.rgb, luma);
    vec3 mixed = mix(src.rgb, tinted, amount);
    fragColor = vec4(mixed, src.a);
}
