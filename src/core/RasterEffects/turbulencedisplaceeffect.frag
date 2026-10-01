#version 330 core
in vec2 texCoord;
out vec4 fragColor;
uniform sampler2D tex;
uniform float amount;   // displacement in px (resolution scaled)
uniform float size;     // noise feature size in px (resolution scaled)
uniform float octaves;  // complexity, 1..8
uniform float zPos;     // evolution phase in noise units
uniform float seed;
uniform int pinMode;    // 0 all, 1 horizontal, 2 vertical, 3 none
uniform float loopEvo;  // 0/1 - cyclic evolution
uniform float cycle;    // revolutions per loop
uniform vec2 imgSize;   // source texture size in px

float grad3(vec3 ip, vec3 dp) {
    vec3 g = fract(sin(vec3(dot(ip, vec3(127.1, 311.7, 74.7)),
                            dot(ip, vec3(269.5, 183.3, 246.1)),
                            dot(ip, vec3(113.5, 271.9, 124.6)))) *
                   43758.5453123) * 2.0 - 1.0;
    return dot(g, dp);
}

float perlin3(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    vec3 u = f*f*(3.0 - 2.0*f);
    return mix(mix(mix(grad3(i + vec3(0.0, 0.0, 0.0), f - vec3(0.0, 0.0, 0.0)),
                       grad3(i + vec3(1.0, 0.0, 0.0), f - vec3(1.0, 0.0, 0.0)), u.x),
                   mix(grad3(i + vec3(0.0, 1.0, 0.0), f - vec3(0.0, 1.0, 0.0)),
                       grad3(i + vec3(1.0, 1.0, 0.0), f - vec3(1.0, 1.0, 0.0)), u.x), u.y),
               mix(mix(grad3(i + vec3(0.0, 0.0, 1.0), f - vec3(0.0, 0.0, 1.0)),
                       grad3(i + vec3(1.0, 0.0, 1.0), f - vec3(1.0, 0.0, 1.0)), u.x),
                   mix(grad3(i + vec3(0.0, 1.0, 1.0), f - vec3(0.0, 1.0, 1.0)),
                       grad3(i + vec3(1.0, 1.0, 1.0), f - vec3(1.0, 1.0, 1.0)), u.x), u.y), u.z);
}

float fbm(vec2 p, float z, float oct) {
    float sum = 0.0, amp = 1.0, norm = 0.0, freq = 1.0;
    for(int o = 0; o < 8; o++) {
        if(float(o) >= oct) break;
        float n = perlin3(vec3(p.x*freq + float(o)*17.31,
                               p.y*freq - float(o)*29.13,
                               z*0.7 + float(o)*11.73));
        sum += n*amp;
        norm += amp;
        amp *= 0.5;
        freq *= 2.0;
    }
    return (norm > 0.0) ? sum/norm : 0.0;
}

float field(vec2 p, float z, float oct) {
    if(loopEvo > 0.5) {
        // cyclic evolution: crossfade two phase-shifted fields so the
        // last frame of a revolution matches the first
        float len = 4.0*max(cycle, 0.01);
        float tt = z/len;
        float t = tt - floor(tt);
        float a = fbm(p, t*len, oct);
        float b = fbm(p, (t - 1.0)*len, oct);
        return a + t*(b - a);
    }
    return fbm(p, z, oct);
}

float pinWeight(vec2 uv) {
    if(pinMode == 0) return sin(3.14159265*uv.x)*sin(3.14159265*uv.y);
    if(pinMode == 1) return sin(3.14159265*uv.x);
    if(pinMode == 2) return sin(3.14159265*uv.y);
    return 1.0;
}

void main(void) {
    vec2 px = texCoord*imgSize;
    vec2 np = px/max(size, 0.5) + vec2(seed*7.31, -seed*3.71);
    float z = zPos + seed*0.917;
    float fx = field(np, z, octaves);
    float fy = field(np + vec2(137.7, -91.3), z, octaves);
    vec2 disp = vec2(fx, fy)*amount*pinWeight(texCoord)/max(imgSize, vec2(1.0));
    fragColor = texture(tex, clamp(texCoord + disp, vec2(0.0), vec2(1.0)));
}
