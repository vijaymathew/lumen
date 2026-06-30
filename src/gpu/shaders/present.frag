#version 440

layout(location = 0) in vec2 v_texcoord;
layout(location = 1) in vec2 v_imagecoord; // 0..1 over the displayed cropped frame
layout(location = 0) out vec4 fragColor;

// Must match present.vert's block verbatim (std140, same fields/order).
layout(std140, binding = 0) uniform buf {
    mat4 mvp;
    mat4 texXform;
    vec4 vig0; // creative vignette: amount, midpoint, roundness, feather
    vec4 vig1; // aspect (W/H), enabled, pad, pad
} ubuf;

layout(binding = 1) uniform sampler2D tex; // the composited offscreen result

void main()
{
    vec4 c = texture(tex, v_texcoord);

    // Creative vignette — mirrors core/Vignette.cpp's applyVignette() (see
    // docs/VIGNETTE.md, the preview==export contract).
    if (ubuf.vig1.y > 0.5) {
        float amount = ubuf.vig0.x, midpoint = ubuf.vig0.y;
        float roundness = ubuf.vig0.z, feather = ubuf.vig0.w;
        float A = ubuf.vig1.x;
        float ax = A >= 1.0 ? A : 1.0;
        float ay = A < 1.0 ? 1.0 / A : 1.0;
        vec2 p = (v_imagecoord - 0.5) * 2.0 * vec2(ax, ay);
        float n = 3.0 - roundness / 100.0;            // 2=circle … 4=boxy
        float d = pow(pow(abs(p.x), n) + pow(abs(p.y), n), 1.0 / n);
        float m = midpoint / 100.0;
        float f = max(feather / 100.0, 1e-3);
        float t = smoothstep(m, m + f, d);
        float gain = clamp(1.0 + (amount / 100.0) * t, 0.0, 4.0);
        c.rgb *= gain;
    }

    fragColor = c;
}
