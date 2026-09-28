#version 330 core
in vec2 texCoord;
out vec4 fragColor;
uniform sampler2D tex;
uniform float amount;

void main(void) {
    vec4 src = texture(tex, texCoord);
    float lum = dot(src.rgb, vec3(0.299, 0.587, 0.114));
    vec3 mixed = mix(src.rgb, vec3(lum), amount);
    fragColor = vec4(mixed, src.a);
}
