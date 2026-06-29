#version 440

layout(location = 0) in vec2 v_texcoord;

layout(location = 0) out vec4 fragColor;

layout(binding = 1) uniform sampler2D tex;
layout(binding = 2) uniform sampler2D lut;       // 256x1 tone curve
layout(binding = 3) uniform sampler3D lut3d;     // 32^3 look LUT
layout(binding = 4) uniform sampler2D selMask;   // selective colour-affinity mask
layout(binding = 5) uniform sampler2D layerMask; // this layer's mask coverage

layout(std140, binding = 0) uniform buf {
    mat4 mvp;
    float exposure;     // EV stops
    float contrast;     // factor, 1 = neutral
    float saturation;   // factor, 1 = neutral
    float lutIntensity; // look blend [0,1]
    float selEnabled;
    float selLow;
    float selHigh;
    float selFeather;
    float selExposure;
    float selContrast;
    float selSaturation;
    float selMaskView;
    float selMaskMode;
    float selInvert;
    float layerOpacity; // this layer's blend onto the running result [0,1]
    float monoEnabled;
    float monoR;        // B&W mix weights (pre-normalised to sum 1)
    float monoG;
    float monoB;
    float monoToneStrength;
    float monoToneR;    // tint colour (pre-normalised to luma 1)
    float monoToneG;
    float monoToneB;
    float wbR;          // white-balance gains (applied before exposure)
    float wbG;
    float wbB;
    float selMaskOpacity; // "show mask" overlay strength [0,1] (= layer opacity)
} ubuf;

const vec3 kLuma = vec3(0.2126, 0.7152, 0.0722);

vec3 applyTone(vec3 c, float exposure, float contrast, float saturation)
{
    c *= exp2(exposure / 2.2);
    c = (c - 0.5) * contrast + 0.5;
    float l = dot(c, kLuma);
    return mix(vec3(l), c, saturation);
}

void main()
{
    vec4 c = texture(tex, v_texcoord);
    vec3 col = c.rgb;

    // Same order and math as the node apply() methods so preview predicts
    // export. 1. Global tone (TuneNode): white balance, then exposure/contrast/sat.
    col *= vec3(ubuf.wbR, ubuf.wbG, ubuf.wbB);
    col = applyTone(col, ubuf.exposure, ubuf.contrast, ubuf.saturation);
    // 2. Tone curves: each channel maps through its own LUT column (R->.r,
    //    G->.g, B->.b). Identity when no curve.
    col = vec3(texture(lut, vec2(col.r, 0.5)).r,
               texture(lut, vec2(col.g, 0.5)).g,
               texture(lut, vec2(col.b, 0.5)).b);
    // 3. Look: trilinear 3D LUT, blended with the pre-look colour by intensity.
    vec3 lutCol = texture(lut3d, clamp(col, 0.0, 1.0)).rgb;
    col = mix(col, lutCol, ubuf.lutIntensity);
    // 3.5 Monochrome: weighted desaturation to grey, then optional toning. Same
    //     math as MonoNode::apply().
    if (ubuf.monoEnabled > 0.5) {
        float g = clamp(dot(col, vec3(ubuf.monoR, ubuf.monoG, ubuf.monoB)), 0.0, 1.0);
        vec3 toned = g * vec3(ubuf.monoToneR, ubuf.monoToneG, ubuf.monoToneB);
        col = mix(vec3(g), toned, ubuf.monoToneStrength);
    }
    // 4. Preview-only "show mask" overlay of the active layer's mask. (The old
    //    in-shader selective adjustment is vestigial — selEnabled stays 0 now
    //    that selective edits are masked layers; the overlay path remains.)
    if (ubuf.selEnabled > 0.5 || ubuf.selMaskView > 0.5) {
        float mask;
        if (ubuf.selMaskMode < 0.5) {
            float L = dot(col, kLuma);
            mask = smoothstep(ubuf.selLow - ubuf.selFeather, ubuf.selLow, L)
                 * (1.0 - smoothstep(ubuf.selHigh, ubuf.selHigh + ubuf.selFeather, L));
        } else {
            mask = texture(selMask, v_texcoord).r; // colour-affinity mask texture
        }
        if (ubuf.selInvert > 0.5)
            mask = 1.0 - mask;
        if (ubuf.selEnabled > 0.5) {
            vec3 adj = applyTone(col, ubuf.selExposure, ubuf.selContrast, ubuf.selSaturation);
            col = mix(col, adj, mask);
        }
        if (ubuf.selMaskView > 0.5) {
            // Build the overlay, then fade the whole thing by the layer's opacity
            // so opacity has a visible effect while the mask is shown (identical
            // to before at selMaskOpacity = 1, vanishing at 0).
            vec3 overlaid = (ubuf.selMaskView < 1.5)
                ? mix(col * 0.4, vec3(0.95, 0.2, 0.2), mask * 0.85) // red over dimmed
                : vec3(mask);                                       // grayscale mask
            col = mix(col, overlaid, ubuf.selMaskOpacity);
        }
    }

    // Composite this layer onto the running result by its mask × opacity. For
    // the Base layer the mask is white and opacity 1, so this is just `col`.
    float layerCov = texture(layerMask, v_texcoord).r * ubuf.layerOpacity;
    fragColor = vec4(mix(c.rgb, col, layerCov), c.a);
}
