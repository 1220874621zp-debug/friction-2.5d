#version 330 core
in vec2 texCoord;
out vec4 fragColor;
uniform sampler2D tex;
uniform float level;

void main(void) {
    vec4 src = texture(tex, texCoord);
    float lum = dot(src.rgb, vec3(0.299, 0.587, 0.114));
    float v = lum >= level ? 1.0 : 0.0;
    fragColor = vec4(vec3(v), src.a);
}
