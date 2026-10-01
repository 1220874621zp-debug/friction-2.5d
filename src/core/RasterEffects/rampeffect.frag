#version 330 core
in vec2 texCoord;
out vec4 fragColor;
uniform sampler2D tex;
uniform vec2 startPoint;
uniform vec4 startColor;
uniform vec2 endPoint;
uniform vec4 endColor;
uniform int shape; // 0 = linear, 1 = radial
uniform float mixOriginal; // 0..1 blend back with the source

void main(void) {
    vec4 src = texture(tex, texCoord);

    // gradient math in pixel space so radial stays circular
    vec2 sz = vec2(textureSize(tex, 0));
    vec2 s = startPoint * sz;
    vec2 d = (endPoint - startPoint) * sz;
    vec2 p = texCoord * sz - s;

    float t;
    if (shape == 0) {
        float len2 = dot(d, d);
        t = len2 > 1e-8 ? clamp(dot(p, d) / len2, 0.0, 1.0) : 0.0;
    } else {
        float radius = length(d);
        t = radius > 1e-6 ? clamp(length(p) / radius, 0.0, 1.0) : 0.0;
    }

    vec4 ramp = mix(startColor, endColor, t); // straight-alpha gradient
    vec4 premul = vec4(ramp.rgb * ramp.a, ramp.a);

    // premultiplied "over": the generated ramp covers the source
    vec4 overResult = premul + src * (1.0 - premul.a);
    fragColor = mix(overResult, src, clamp(mixOriginal, 0.0, 1.0));
}
