#version 330 core
in vec2 texCoord;
out vec4 fragColor;
uniform sampler2D tex;
uniform float transition;
uniform float width;
uniform float angle;
uniform float feather;

void main(void) {
    vec4 src = texture(tex, texCoord);
    
    float rad = radians(angle);
    vec2 dir = vec2(cos(rad), sin(rad));
    float proj = dot(texCoord, dir);

    float stripePos = fract(proj * width);
    float edge = 1.0 - transition;
    float f = max(0.001, feather * 0.5);

    float alpha = smoothstep(edge - f, edge + f, stripePos);
    fragColor = vec4(src.rgb, src.a * (1.0 - alpha));
}
