#version 330 core
in vec2 texCoord;
out vec4 fragColor;
uniform sampler2D tex;
uniform int channel;
uniform float inBlack;
uniform float inWhite;
uniform float midGamma;
uniform float outBlack;
uniform float outWhite;

float levelMap(const float v) {
    float t = clamp((v - inBlack) / (inWhite - inBlack), 0.0, 1.0);
    t = pow(t, 1.0 / midGamma);
    return outBlack + t * (outWhite - outBlack);
}

void main(void) {
    vec4 src = texture(tex, texCoord);
    vec3 c = src.rgb;
    vec3 m = vec3(levelMap(c.r), levelMap(c.g), levelMap(c.b));
    vec3 rgb = c;
    if (channel == 0) { rgb = m; }
    else if (channel == 1) { rgb.r = m.r; }
    else if (channel == 2) { rgb.g = m.g; }
    else { rgb.b = m.b; }
    fragColor = vec4(rgb, src.a);
}
