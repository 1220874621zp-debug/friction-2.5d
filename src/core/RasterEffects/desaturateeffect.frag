#version 330 core
in vec2 texCoord;
out vec4 fragColor;
uniform sampler2D tex;
uniform float amount;
uniform bool invert;

void main(void) {
    vec4 src = texture(tex, texCoord);
    float lum = dot(src.rgb, vec3(0.299, 0.587, 0.114));
    if (invert) { lum = 1.0 - lum; }
    vec3 mixed = mix(src.rgb, vec3(lum), amount);
    fragColor = vec4(mixed, src.a);
}
