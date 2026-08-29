#version 330 core
in vec2 texCoord;
out vec4 fragColor;
uniform sampler2D tex;
uniform vec2 pixelStep;

void main(void) {
    if (pixelStep.x <= 0.0001 || pixelStep.y <= 0.0001) {
        fragColor = texture(tex, texCoord);
        return;
    }
    vec2 coord = floor(texCoord / pixelStep) * pixelStep + pixelStep * 0.5;
    fragColor = texture(tex, coord);
}
