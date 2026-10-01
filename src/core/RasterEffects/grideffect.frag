#version 330 core
in vec2 texCoord;
out vec4 fragColor;
uniform sampler2D tex;
uniform vec2 anchor;      // 0..1 UV
uniform vec2 cellSize;    // pixels (resolution-scaled)
uniform float border;     // pixels (resolution-scaled)
uniform vec4 color;
uniform float invert;     // 0/1
uniform float mixOriginal; // 0..1 blend back with the source

void main(void) {
    vec4 src = texture(tex, texCoord);

    vec2 sz = vec2(textureSize(tex, 0));
    vec2 rel = texCoord * sz - anchor * sz;
    vec2 cell = max(cellSize, vec2(1.0));

    // distance to the nearest grid line, wrapped into one cell
    vec2 m = mod(rel, cell);
    vec2 dEdge = min(m, cell - m);
    float d = min(dEdge.x, dEdge.y);

    float halfW = max(border * 0.5, 0.0);
    // 1.5px smoothstep ramp for antialiasing
    float line = 1.0 - smoothstep(halfW - 0.75, halfW + 0.75, d);
    line = clamp(line, 0.0, 1.0);
    if (invert > 0.5) line = 1.0 - line;

    // premultiplied grid color masked by the line coverage
    vec4 grid = vec4(color.rgb * color.a, color.a) * line;

    // premultiplied "over": the generated grid covers the source
    vec4 overResult = grid + src * (1.0 - grid.a);
    fragColor = mix(overResult, src, clamp(mixOriginal, 0.0, 1.0));
}
