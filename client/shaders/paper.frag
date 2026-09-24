#version 440

// Procedural paper grain (PaperOverlay.qml). An OVERLAY, not a surface:
// it darkens, lightens and stains whatever is painted beneath it, so the
// same shader papers a panel, a card and a chip in any banner colour.
// Four registers of cheap value noise - blotch (broad pulp unevenness),
// mottle (mid-scale cloudiness), fibres (faint threads along both axes),
// grain (per-pixel tooth) - plus a rare aged stain. `seed` offsets the
// noise field, so every instance is its own sheet.

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec2 pxSize;    // item size in px: the noise is scaled in pixels
    float seed;     // which sheet of paper this is
    float radius;   // rounded-corner clip in px (0 = square)
    float strength; // overall effect amplitude, 1 = house default
} u;

float hash12(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float vnoise(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 s = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash12(i),               hash12(i + vec2(1, 0)), s.x),
               mix(hash12(i + vec2(0, 1)),  hash12(i + vec2(1, 1)), s.x),
               s.y);
}

float fbm(vec2 p)
{
    float v = 0.0;
    float amp = 0.5;
    for (int i = 0; i < 4; ++i) {
        v += amp * vnoise(p);
        p = p * 2.17 + vec2(11.3, 7.9);
        amp *= 0.5;
    }
    return v;
}

void main()
{
    vec2 px = qt_TexCoord0 * u.pxSize + vec2(u.seed * 127.1, u.seed * 311.7);

    float blotch = fbm(px * 0.013) - 0.5;
    float mottle = fbm(px * 0.055) - 0.5;
    float fibres = (vnoise(vec2(px.x * 0.9,  px.y * 0.055)) - 0.5)
                 + (vnoise(vec2(px.x * 0.05, px.y * 0.85)) - 0.5);
    float grain  = hash12(floor(px * 1.3)) - 0.5;

    float relief = (blotch * 0.34 + mottle * 0.22 + fibres * 0.12
                    + grain * 0.16) * u.strength;

    float stain = smoothstep(0.58, 0.80, fbm(px * 0.006 + vec2(37.7, 17.3)))
                  * 0.14 * u.strength;

    float dark  = clamp(-relief, 0.0, 1.0);
    float light = clamp(relief, 0.0, 1.0);
    vec3 src = vec3(1.00, 0.96, 0.88) * light + vec3(0.36, 0.28, 0.16) * stain;
    float a = min(light + dark + stain, 0.6);

    float mask = 1.0;
    if (u.radius > 0.0) {
        vec2 h = u.pxSize * 0.5;
        vec2 q = abs(qt_TexCoord0 * u.pxSize - h) - (h - vec2(u.radius));
        float d = length(max(q, vec2(0.0))) - u.radius;
        mask = clamp(0.5 - d, 0.0, 1.0);
    }

    fragColor = vec4(src, a) * (mask * u.qt_Opacity);
}
