#version 330 core
in vec2 texCoord;
out vec4 fragColor;
uniform sampler2D tex;
uniform float hue;        // degrees, -180..180
uniform float saturation; // percent, -1..1
uniform float lightness;  // percent, -1..1

vec3 rgb2hsl(const vec3 c) {
    const float maxc = max(c.r, max(c.g, c.b));
    const float minc = min(c.r, min(c.g, c.b));
    const float l = (maxc + minc) * 0.5;
    if (maxc == minc) { return vec3(0.0, 0.0, l); }
    const float d = maxc - minc;
    const float s = l > 0.5 ? d / (2.0 - maxc - minc)
                            : d / (maxc + minc);
    float h;
    if (maxc == c.r)      { h = (c.g - c.b) / d + (c.g < c.b ? 6.0 : 0.0); }
    else if (maxc == c.g) { h = (c.b - c.r) / d + 2.0; }
    else                  { h = (c.r - c.g) / d + 4.0; }
    return vec3(h / 6.0, s, l);
}

float hue2rgb(const float p, const float q, float t) {
    if (t < 0.0) { t += 1.0; }
    if (t > 1.0) { t -= 1.0; }
    if (t < 1.0 / 6.0) { return p + (q - p) * 6.0 * t; }
    if (t < 1.0 / 2.0) { return q; }
    if (t < 2.0 / 3.0) { return p + (q - p) * (2.0 / 3.0 - t) * 6.0; }
    return p;
}

vec3 hsl2rgb(const vec3 hsl) {
    if (hsl.y == 0.0) { return vec3(hsl.z); }
    const float q = hsl.z < 0.5 ? hsl.z * (1.0 + hsl.y)
                                : hsl.z + hsl.y - hsl.z * hsl.y;
    const float p = 2.0 * hsl.z - q;
    return vec3(hue2rgb(p, q, hsl.x + 1.0 / 3.0),
                hue2rgb(p, q, hsl.x),
                hue2rgb(p, q, hsl.x - 1.0 / 3.0));
}

void main(void) {
    vec4 c = texture(tex, texCoord);
    if (c.a <= 0.0) { fragColor = c; return; }
    // work on straight colors: premultiplied rgb would skew hue/sat
    vec3 hsl = rgb2hsl(clamp(c.rgb / c.a, 0.0, 1.0));
    hsl.x = mod(hsl.x + hue / 360.0, 1.0);
    hsl.y = saturation < 0.0 ? hsl.y * (1.0 + saturation)
                             : hsl.y + (1.0 - hsl.y) * saturation;
    hsl.z = lightness < 0.0 ? hsl.z * (1.0 + lightness)
                            : hsl.z + (1.0 - hsl.z) * lightness;
    fragColor = vec4(hsl2rgb(hsl) * c.a, c.a);
}
