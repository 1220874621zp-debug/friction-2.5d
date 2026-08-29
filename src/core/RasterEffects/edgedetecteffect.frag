#version 330 core
in vec2 texCoord;
out vec4 fragColor;
uniform sampler2D tex;
uniform vec2 stepSize;
uniform float sensitivity;
uniform float invert;
uniform vec4 edgeColor;
uniform vec4 bgColor;

float luma(vec2 uv) {
    vec4 c = texture(tex, uv);
    return dot(c.rgb, vec3(0.2126, 0.7152, 0.0722)) * c.a;
}

void main(void) {
    float gx = -1.0 * luma(texCoord + vec2(-stepSize.x, -stepSize.y))
             +  1.0 * luma(texCoord + vec2( stepSize.x, -stepSize.y))
             -  2.0 * luma(texCoord + vec2(-stepSize.x, 0.0))
             +  2.0 * luma(texCoord + vec2( stepSize.x, 0.0))
             -  1.0 * luma(texCoord + vec2(-stepSize.x,  stepSize.y))
             +  1.0 * luma(texCoord + vec2( stepSize.x,  stepSize.y));

    float gy = -1.0 * luma(texCoord + vec2(-stepSize.x, -stepSize.y))
             -  2.0 * luma(texCoord + vec2( 0.0,        -stepSize.y))
             -  1.0 * luma(texCoord + vec2( stepSize.x, -stepSize.y))
             +  1.0 * luma(texCoord + vec2(-stepSize.x,  stepSize.y))
             +  2.0 * luma(texCoord + vec2( 0.0,         stepSize.y))
             +  1.0 * luma(texCoord + vec2( stepSize.x,  stepSize.y));

    float edge = clamp(length(vec2(gx, gy)) * sensitivity, 0.0, 1.0);
    if (invert > 0.5) {
        edge = 1.0 - edge;
    }

    vec4 src = texture(tex, texCoord);
    vec3 mixed = mix(bgColor.rgb, edgeColor.rgb, edge);
    fragColor = vec4(mixed, max(edge, src.a * bgColor.a));
}
