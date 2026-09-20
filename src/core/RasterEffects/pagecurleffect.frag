#version 330 core
in vec2 texCoord;
out vec4 fragColor;
uniform sampler2D tex;

// Foldspace-style surface roll, distilled to a cylindrical page curl:
// the image wraps around a cylinder whose contact line sweeps across
// the page (analytic inverse mapping, orthographic view, wrapped turns
// z-sorted per pixel, N.L shading + specular, solid back-face color,
// cast shadow under the raised tube). The input texture is
// premultiplied alpha and so is the output.
//
// NOTE: no const locals with runtime initializers - the GLSL 3.30
// spec requires constant expressions for const, and some drivers
// enforce it.

uniform float uProgress;   // 0..1, 0 = flat passthrough
uniform float uDirection;  // roll travel direction in degrees (0 = right edge rolls leftward)
uniform float uRadius;     // cylinder radius, fraction of image height
uniform vec3  uBackColor;  // straight-alpha color of the back face
uniform float uLightAngle; // light azimuth in degrees (225 = from the upper left)
uniform float uLightElev;  // light elevation above the page plane, degrees
uniform float uAmbient;    // 0..1
uniform float uShadow;     // 0..1 cast-shadow strength on the flat part
uniform float uSpecular;   // 0..1
uniform vec2  uTexSize;    // source size in pixels

const float PI = 3.14159265359;
const float TWO_PI = 6.28318530718;

vec4 sampleTex(vec2 uv) {
    // manual bilinear so the result does not depend on the texture
    // filter the engine happens to use; clamped to the edge texels
    vec2 t = uv * uTexSize - vec2(0.5);
    t = clamp(t, vec2(0.0), uTexSize - vec2(1.0));
    vec2 base = floor(t);
    vec2 f = t - base;
    vec2 st = vec2(1.0) / uTexSize;
    vec4 c0 = texture(tex, (base + vec2(0.5, 0.5)) * st);
    vec4 c1 = texture(tex, (base + vec2(1.5, 0.5)) * st);
    vec4 c2 = texture(tex, (base + vec2(0.5, 1.5)) * st);
    vec4 c3 = texture(tex, (base + vec2(1.5, 1.5)) * st);
    return mix(mix(c0, c1, f.x), mix(c2, c3, f.x), f.y);
}

void main(void) {
    float aspect = uTexSize.x / uTexSize.y;
    // pixel space scaled to units of image height (square pixels)
    vec2 p = vec2(texCoord.x * aspect, texCoord.y);

    float radDir = radians(uDirection);
    vec2 dir = vec2(cos(radDir), sin(radDir));
    vec2 perp = vec2(-dir.y, dir.x);

    // how far the contact line can travel: page extent along dir
    float cMax = max(max(dot(vec2(0.0, 0.0), dir),
                         dot(vec2(aspect, 0.0), dir)),
                     max(dot(vec2(0.0, 1.0), dir),
                         dot(vec2(aspect, 1.0), dir)));
    float c = (1.0 - uProgress) * cMax;
    float R = max(uRadius, 0.002);

    float d = dot(p, dir) - c;      // signed distance from the contact line
    float m = dot(p, perp);         // position along the curl axis

    // the wrapped page lands at -R*sin(phi): solve for every turn and
    // keep the frontmost (largest height R*(1-cos(phi)))
    float bestZ = -1.0;
    float bestPhi = -1.0;
    float q = clamp(-d / R, -1.0, 1.0);
    float phi0 = asin(q);
    float phiCap = 11.5 * PI;       // ~5.75 turns is plenty
    for (int k = 0; k < 8; k++) {
        float base = TWO_PI * float(k);
        float f1 = phi0 + base;
        if (f1 > phiCap) break;
        if (f1 >= 0.0) {
            float z = R * (1.0 - cos(f1));
            if (z > bestZ) { bestZ = z; bestPhi = f1; }
        }
        float f2 = PI - phi0 + base;
        if (f2 > phiCap) break;
        if (f2 >= 0.0) {
            float z = R * (1.0 - cos(f2));
            if (z > bestZ) { bestZ = z; bestPhi = f2; }
        }
    }

    // the wrapped point is material only if its source stays on the page
    bool useWrap = bestPhi >= 0.0;
    vec2 uvSrc = texCoord;
    if (useWrap) {
        vec2 P0 = dir * (c + R * bestPhi) + perp * m;
        vec2 uv0 = vec2(P0.x / aspect, P0.y);
        if (uv0.x < 0.0 || uv0.x > 1.0 || uv0.y < 0.0 || uv0.y > 1.0) {
            useWrap = false;
        } else {
            uvSrc = uv0;
        }
    }

    // no tube above and not on the flat side: the page has left
    if (!useWrap && d > 0.0) {
        fragColor = vec4(0.0);
        return;
    }

    // light (image-space y points down; 225 deg = from the upper left)
    float la = radians(uLightAngle);
    float el = radians(uLightElev);
    vec2 Lxy = vec2(cos(la), sin(la)) * cos(el);
    float Lz = sin(el);
    float Ls = dot(Lxy, dir);
    // half vector with the orthographic view direction (0,0,1)
    vec2 Hxy = Lxy;
    float Hz = Lz + 1.0;
    float hLen = max(sqrt(dot(Hxy, Hxy) + Hz * Hz), 0.0001);
    Hxy /= hLen; Hz /= hLen;
    float Hs = dot(Hxy, dir);

    vec4 src = sampleTex(uvSrc);

    float shade;
    float spec = 0.0;
    bool backFace = false;
    if (useWrap) {
        float sinPhi = sin(bestPhi);
        float cosPhi = cos(bestPhi);
        float nDotL = sinPhi * Ls + cosPhi * Lz;
        float nDotH = sinPhi * Hs + cosPhi * Hz;
        backFace = cosPhi < 0.0;          // the flipped side faces the camera
        if (backFace) { nDotL = -nDotL; nDotH = -nDotH; }
        shade = uAmbient + (1.0 - uAmbient) * max(0.0, nDotL);
        spec = uSpecular * pow(max(0.0, nDotH), 32.0);
    } else {
        // flat part: fade the lighting in with the amount of wrapped
        // material so progress 0 is an exact passthrough
        float arcAvail = (cMax - c) / R;
        float ta = clamp(arcAvail / PI, 0.0, 1.0);
        float curlActive = ta * ta * (3.0 - 2.0 * ta);
        float litFlat = uAmbient + (1.0 - uAmbient) * max(0.0, Lz);
        shade = mix(1.0, litFlat, curlActive);
        // the raised tube shades the flat part behind the contact line:
        // cast a ray from the pixel toward the light, test the cylinder
        if (curlActive > 0.0) {
            float A = Ls * d - Lz * R;
            float disc = A * A - d * d;
            if (disc > 0.0) {
                float sd = sqrt(disc);
                if (sd > A) {             // a positive ray parameter exists
                    float pen = clamp(0.2 * R, 0.004, 0.05);
                    float tp = clamp(sd / pen, 0.0, 1.0);
                    float occ = 1.0 - tp * tp * (3.0 - 2.0 * tp);
                    shade *= 1.0 - uShadow * curlActive * occ;
                }
            }
        }
    }

    if (backFace) {
        float a = src.a;
        fragColor = vec4(uBackColor * (a * shade) + vec3(spec * a), a);
    } else {
        fragColor = vec4(src.rgb * shade + vec3(spec * src.a), src.a);
    }
}
