#version 330 core
in vec2 texCoord;
out vec4 fragColor;
uniform sampler2D tex;
uniform float exposure;
uniform float contrast;
uniform float saturation;
uniform float temperature;
uniform float tint;

void main(void) {
    vec4 src = texture(tex, texCoord);
    vec3 col = src.rgb;

    // 1. Exposure (f-stops)
    col *= pow(2.0, exposure);

    // 2. Temperature (warm/cool) & Tint (green/magenta)
    col.r += temperature * 0.1;
    col.b -= temperature * 0.1;
    col.g -= tint * 0.1;
    col.r += tint * 0.05;
    col.b += tint * 0.05;

    // 3. Contrast around mid-gray (0.18 linear / 0.5 sRGB)
    col = (col - 0.5) * (1.0 + contrast) + 0.5;

    // 4. Saturation
    float luma = dot(col, vec3(0.2126, 0.7152, 0.0722));
    col = mix(vec3(luma), col, max(0.0, 1.0 + saturation));

    fragColor = vec4(clamp(col, 0.0, 1.0), src.a);
}
