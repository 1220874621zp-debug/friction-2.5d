#version 330 core
in vec2 texCoord;
out vec4 fragColor;
uniform sampler2D tex;
uniform float amount;
uniform float timeSeed;
uniform float monochrome;

float hash(vec2 p) {
    return fract(sin(dot(p + vec2(timeSeed, timeSeed * 0.7), vec2(12.9898, 78.233))) * 43758.5453);
}

void main(void) {
    vec4 src = texture(tex, texCoord);
    float n1 = (hash(texCoord) - 0.5) * 2.0;
    float n2 = (hash(texCoord + vec2(0.123, 0.456)) - 0.5) * 2.0;
    float n3 = (hash(texCoord + vec2(0.789, 0.321)) - 0.5) * 2.0;

    vec3 n = (monochrome > 0.5) ? vec3(n1) : vec3(n1, n2, n3);
    vec3 outRgb = src.rgb + n * amount * max(0.2, src.a);
    fragColor = vec4(clamp(outRgb, 0.0, 1.0), src.a);
}
