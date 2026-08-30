#version 330 core
in vec2 texCoord;
out vec4 fragColor;
uniform sampler2D tex;
uniform float rRad;
uniform float gRad;
uniform float bRad;

float blurChannel(int ch, float rad) {
    if (rad <= 0.0001) {
        vec4 c = texture(tex, texCoord);
        return (ch == 0) ? c.r : ((ch == 1) ? c.g : c.b);
    }
    float sum = 0.0;
    float totalWeight = 0.0;
    for (float x = -3.0; x <= 3.0; x += 1.0) {
        for (float y = -3.0; y <= 3.0; y += 1.0) {
            float w = exp(-(x * x + y * y) / (2.0 * 2.0));
            vec2 offset = vec2(x, y) * rad * 0.002;
            vec4 s = texture(tex, clamp(texCoord + offset, 0.0, 1.0));
            float val = (ch == 0) ? s.r : ((ch == 1) ? s.g : s.b);
            sum += val * w;
            totalWeight += w;
        }
    }
    return sum / max(0.001, totalWeight);
}

void main(void) {
    float r = blurChannel(0, rRad);
    float g = blurChannel(1, gRad);
    float b = blurChannel(2, bRad);
    float a = texture(tex, texCoord).a;
    fragColor = vec4(r, g, b, a);
}
