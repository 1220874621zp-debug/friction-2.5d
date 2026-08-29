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

    vec2 offsets[12] = vec2[](
        vec2( 0.0,  1.0), vec2( 0.0, -1.0), vec2( 1.0,  0.0), vec2(-1.0,  0.0),
        vec2( 0.7,  0.7), vec2(-0.7,  0.7), vec2( 0.7, -0.7), vec2(-0.7, -0.7),
        vec2( 0.4,  0.9), vec2(-0.4,  0.9), vec2( 0.4, -0.9), vec2(-0.4, -0.9)
    );

    for (int i = 0; i < 12; i++) {
        vec2 uv = texCoord + offsets[i] * radius;
        vec4 smp = texture(tex, uv);
        float luma = dot(smp.rgb, vec3(0.2126, 0.7152, 0.0722));
        float pass = max(0.0, luma - threshold);
        bloom += smp.rgb * pass;
        totalWeight += 1.0;
    }

    bloom = (bloom / totalWeight) * intensity * glowColor.rgb;
    vec3 outRgb = src.rgb + bloom * max(0.2, src.a);
    fragColor = vec4(clamp(outRgb, 0.0, 1.0), src.a);
}
