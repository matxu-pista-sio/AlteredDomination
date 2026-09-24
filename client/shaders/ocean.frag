#version 440

// The ocean under the chart (Ocean.qml, docs/UI_THEME.md "The map stack"):
// deep water shading toward the poles, two layers of slow value-noise
// swell, and a faint graticule every 15 degrees of the Miller projection.
// `time` drives the swell; with waves off the item stops the clock and the
// shader paints flat colour (the noise layers are skipped by `waves`).

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec2 mapSize;     // map units covered by the item (whole map: width, height)
    vec2 mapOrigin;   // map units at the item's top-left corner
    vec4 deep;        // ocean colour at depth
    vec4 shallow;     // ocean colour near the surface
    float time;       // seconds
    float waves;      // 1 = animated swell, 0 = flat
    float graticule;  // 0..1 line strength
    float zoom;       // camera zoom (map units -> px), for line width
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

void main()
{
    vec2 m = u.mapOrigin + qt_TexCoord0 * u.mapSize;   // map units
    // latitude-ish shading: darker toward the edges of the chart
    float ny = m.y / 2019.0;
    float depth = smoothstep(0.0, 0.5, ny) * smoothstep(1.0, 0.5, ny);
    vec3 col = mix(u.deep.rgb, u.shallow.rgb, depth * 0.75);

    if (u.waves > 0.5) {
        float t = u.time * 0.05;
        float n1 = vnoise(m * 0.022 + vec2(t, -t * 0.7));
        float n2 = vnoise(m * 0.067 - vec2(t * 1.3, t * 0.4));
        float n3 = vnoise(m * 0.19 + vec2(-t * 0.8, t * 1.1));
        float swell = (n1 - 0.5) * 0.045 + (n2 - 0.5) * 0.028 + (n3 - 0.5) * 0.014;
        col += swell;
        // scattered glints where two crests meet
        float glint = smoothstep(0.80, 0.93, n1 * n2 * 2.2);
        col += glint * 0.04;
    }

    // graticule: 15 degrees of longitude = 4096/24 map units; latitude lines
    // follow the projection's y (approximately even in Miller)
    float cell = 4096.0 / 24.0;
    vec2 g = abs(fract(m / cell + 0.5) - 0.5) * cell * u.zoom;  // px to the nearest line
    float line = 1.0 - smoothstep(0.0, 1.2, min(g.x, g.y));
    col += line * u.graticule * 0.09;

    fragColor = vec4(col, 1.0) * u.qt_Opacity;
}
