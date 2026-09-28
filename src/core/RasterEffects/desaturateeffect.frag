#version 330 core
in vec2 texCoord;
out vec4 fragColor;
uniform sampler2D tex;
uniform float amount;
uniform bool invert;

void main(void) {
    vec4 src = texture(tex, texCoord);
    vec3 target;
    if (invert) {
        target = src.rgb - vec3(min(src.r, min(src.g, src.b)));
    } else {
        target = vec3(dot(src.rgb, vec3(0.299, 0.587, 0.114)));
    }
    vec3 mixed = mix(src.rgb, target, amount);
    fragColor = vec4(mixed, src.a);
}
