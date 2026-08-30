#version 330 core
in vec2 texCoord;
out vec4 fragColor;
uniform sampler2D tex;
uniform float threshold;
uniform float intensity;
uniform float radius;
uniform vec4 glowColor;

void main(void) {
    vec4 src = texture(tex, texCoord);
    vec3 bloom = vec3(0.0);
    float totalWeight = 0.0;

    const int SAMPLES = 16;
    vec2 offsets[16] = vec2[](
        vec2( 0.0,  1.0), vec2( 0.0, -1.0), vec2( 1.0,  0.0), vec2(-1.0,  0.0),
        vec2( 0.707,  0.707), vec2(-0.707,  0.707), vec2( 0.707, -0.707), vec2(-0.707, -0.707),
        vec2( 0.0,  2.0), vec2( 0.0, -2.0), vec2( 2.0,  0.0), vec2(-2.0,  0.0),
        vec2( 1.414,  1.414), vec2(-1.414,  1.414), vec2( 1.414, -1.414), vec2(-1.414, -1.414)
    );

    float weights[16] = float[](
        0.12, 0.12, 0.12, 0.12,
        0.08, 0.08, 0.08, 0.08,
        0.05, 0.05, 0.05, 0.05,
        0.03, 0.03, 0.03, 0.03
    );

    for (int i = 0; i < 16; i++) {
        vec2 uv = texCoord + offsets[i] * radius;
        vec4 smp = texture(tex, uv);
        float luma = dot(smp.rgb, vec3(0.2126, 0.7152, 0.0722));
        float pass = max(0.0, luma - threshold) / max(0.001, 1.0 - threshold);
        bloom += smp.rgb * smp.a * pass * weights[i];
        totalWeight += weights[i];
    }

    bloom = (bloom / totalWeight) * intensity * glowColor.rgb;
    vec3 outRgb = src.rgb + bloom;
    fragColor = vec4(clamp(outRgb, 0.0, 1.0), max(src.a, clamp(length(bloom), 0.0, 1.0)));
}
