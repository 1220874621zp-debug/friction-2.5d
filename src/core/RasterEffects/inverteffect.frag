#version 330 core
in vec2 texCoord;
out vec4 fragColor;
uniform sampler2D tex;
uniform float amount;

void main(void) {
    vec4 src = texture(tex, texCoord);
    vec3 inv = vec3(1.0) - src.rgb;
    vec3 mixed = mix(src.rgb, inv, amount);
    fragColor = vec4(mixed, src.a);
}
