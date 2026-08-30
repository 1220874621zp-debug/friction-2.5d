#version 330 core
in vec2 texCoord;
out vec4 fragColor;
uniform sampler2D tex;
uniform float center;
uniform float angle;
uniform float width;
uniform float intensity;
uniform float feather;
uniform vec4 lightColor;

void main(void) {
    vec4 src = texture(tex, texCoord);
    if (src.a < 0.001) {
        fragColor = vec4(0.0);
        return;
    }

    float rad = radians(angle);
    vec2 dir = vec2(cos(rad), sin(rad));
    vec2 normal = vec2(-sin(rad), cos(rad));

    vec2 uv = texCoord - vec2(0.5);
    float proj = dot(uv, normal);
    float sweepPos = (center - 50.0) * 0.02;

    float dist = abs(proj - sweepPos);
    float halfW = max(width * 0.005, 0.001);
    float fth = max(feather * 0.01 * halfW, 0.0001);

    float beam = smoothstep(halfW + fth, halfW - fth, dist);
    float core = smoothstep(halfW * 0.4, 0.0, dist) * 0.5;
    float lightVal = (beam + core) * (intensity * 0.01);

    vec3 blended = src.rgb + lightColor.rgb * lightVal * lightColor.a;
    fragColor = vec4(clamp(blended, 0.0, 1.0), src.a);
}
