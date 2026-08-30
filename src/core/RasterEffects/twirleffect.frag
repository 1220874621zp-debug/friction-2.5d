#version 330 core
in vec2 texCoord;
out vec4 fragColor;
uniform sampler2D tex;
uniform vec2 center;
uniform float radius;
uniform float angle;

void main(void) {
    vec2 d = texCoord - center;
    float dist = length(d);
    if (dist < radius && radius > 0.0001) {
        float percent = (radius - dist) / radius;
        float theta = percent * percent * angle;
        float s = sin(theta);
        float c = cos(theta);
        d = vec2(d.x * c - d.y * s, d.x * s + d.y * c);
    }
    fragColor = texture(tex, clamp(center + d, 0.0, 1.0));
}
