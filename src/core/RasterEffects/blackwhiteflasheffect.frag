#version 330 core
in vec2 texCoord;
out vec4 fragColor;
uniform sampler2D tex;
uniform float threshold;
uniform float contrast;
uniform float lightIntensity;
uniform float lightLength;
uniform float edgeIntensity;
uniform float invert;
uniform vec4 flashColor;
uniform vec4 bgColor;

float getLum(vec2 uv) {
    vec4 c = texture(tex, clamp(uv, 0.0, 1.0));
    float lum = dot(c.rgb, vec3(0.299, 0.587, 0.114));
    float cl = (lum - 0.5) * max(contrast, 0.1) + 0.5;
    return (cl > threshold * 0.01) ? 1.0 : 0.0;
}

void main(void) {
    vec4 src = texture(tex, texCoord);
    if (src.a < 0.001) {
        fragColor = vec4(0.0);
        return;
    }

    vec2 texSize = vec2(textureSize(tex, 0));
    vec2 invSize = 1.0 / max(texSize, vec2(1.0));

    float centerVal = getLum(texCoord);

    // Sobel gradient for normal emission
    float l_l = getLum(texCoord + vec2(-invSize.x, 0.0));
    float l_r = getLum(texCoord + vec2(invSize.x, 0.0));
    float l_d = getLum(texCoord + vec2(0.0, -invSize.y));
    float l_u = getLum(texCoord + vec2(0.0, invSize.y));

    vec2 grad = vec2(l_r - l_l, l_u - l_d);
    float edge = length(grad);

    // Ray emission along normal
    float rayEmission = 0.0;
    if (lightIntensity > 0.01 && lightLength > 0.5) {
        vec2 norm = (edge > 0.01) ? normalize(grad) : vec2(0.0);
        float stepLen = 2.0;
        int maxSteps = int(clamp(lightLength / stepLen, 1.0, 40.0));
        for (int i = 1; i <= maxSteps; ++i) {
            float dist = float(i) * stepLen;
            vec2 samplePos = texCoord - norm * (dist * invSize);
            float sVal = getLum(samplePos);
            float diff = abs(centerVal - sVal);
            float atten = exp(-dist / max(lightLength * 0.35, 1.0));
            rayEmission += diff * atten;
        }
        rayEmission = clamp(rayEmission * (lightIntensity * 0.1) * edgeIntensity, 0.0, 1.0);
    }

    float finalVal = centerVal + rayEmission + edge * edgeIntensity * 0.5;
    finalVal = clamp(finalVal, 0.0, 1.0);
    if (invert > 0.5) finalVal = 1.0 - finalVal;

    vec4 outCol = mix(bgColor, flashColor, finalVal);
    fragColor = vec4(clamp(outCol.rgb, 0.0, 1.0), src.a * outCol.a);
}
