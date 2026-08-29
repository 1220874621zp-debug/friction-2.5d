#version 330 core
in vec2 texCoord;
out vec4 fragColor;
uniform sampler2D tex;
uniform float dotSize;
uniform float angle;
uniform float contrast;
uniform vec2 resolution;

void main(void) {
    vec4 src = texture(tex, texCoord);
    float rad = radians(angle);
    mat2 rot = mat2(cos(rad), -sin(rad), sin(rad), cos(rad));
    vec2 p = rot * (texCoord * resolution);
    vec2 grid = mod(p, dotSize) - vec2(dotSize * 0.5);
    float dist = length(grid) / (dotSize * 0.5);

    float luma = dot(src.rgb, vec3(0.2126, 0.7152, 0.0722));
    float threshold = 1.0 - luma;
    float smoothW = max(0.01, (1.0 - contrast * 0.01) * 0.3);
    float dotVal = smoothstep(threshold - smoothW, threshold + smoothW, dist);
    fragColor = vec4(vec3(dotVal), src.a);
}
