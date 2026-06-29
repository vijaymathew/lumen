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
    float wb00;         // white-balance 3x3 (row-major, linear light, pre-exposure)
    float wb01;
    float wb02;
    float wb10;
    float wb11;
    float wb12;
    float wb20;
    float wb21;
    float wb22;
    float gradeEnabled;   // colour grade (Lift/Gamma/Gain = Slope/Offset/Power)
    float gradeSlope0;
    float gradeSlope1;
    float gradeSlope2;
    float gradeOffset0;
    float gradeOffset1;
    float gradeOffset2;
    float gradePower0;
    float gradePower1;
    float gradePower2;
    float selMaskOpacity; // "show mask" overlay strength [0,1] (= layer opacity)
    float vibrance;       // saturation-aware boost amount (vibrance/100), 0 = neutral
    float monoBand0;      // per-color B&W mix: 8 hue bands at 0/45/.../315°, [-1,1]
    float monoBand1;
    float monoBand2;
    float monoBand3;
    float monoBand4;
    float monoBand5;
    float monoBand6;
    float monoBand7;
} ubuf;

const vec3 kLuma = vec3(0.2126, 0.7152, 0.0722);

vec3 applyTone(vec3 c, float exposure, float contrast, float saturation, float vibrance)
{
    c *= exp2(exposure / 2.2);
    c = (c - 0.5) * contrast + 0.5;
    float l = dot(c, kLuma);
    c = mix(vec3(l), c, saturation);
    // Vibrance: push low-saturation pixels more, ease off already-saturated ones.
    // Matches TuneNode::applyVibrance.
    if (vibrance != 0.0) {
        float sat = max(c.r, max(c.g, c.b)) - min(c.r, min(c.g, c.b));
        float f = max(0.0, 1.0 + vibrance * (1.0 - sat));
        float l2 = dot(c, kLuma);
        c = mix(vec3(l2), c, f);
    }
    return c;
}

// Hue of a colour in degrees [0,360) — matches MonoNode::hue6.
float monoHue(vec3 c)
{
    float mx = max(c.r, max(c.g, c.b));
    float mn = min(c.r, min(c.g, c.b));
    float d = mx - mn;
    if (d < 1e-6)
        return 0.0;
    float h;
    if (mx == c.r)
        h = mod((c.g - c.b) / d, 6.0);
    else if (mx == c.g)
        h = (c.b - c.r) / d + 2.0;
    else
        h = (c.r - c.g) / d + 4.0;
    h *= 60.0;
    if (h < 0.0)
        h += 360.0;
    return h;
}

// Per-color mix weight at `hue`: linear interpolation between the two adjacent
// colour anchors at their true hue angles (last segment wraps 330°→360°) —
// matches MonoNode::bandShift.
float monoBandShift(float hue)
{
    float b[8] = float[8](ubuf.monoBand0, ubuf.monoBand1, ubuf.monoBand2, ubuf.monoBand3,
                          ubuf.monoBand4, ubuf.monoBand5, ubuf.monoBand6, ubuf.monoBand7);
    float c[8] = float[8](0.0, 30.0, 60.0, 120.0, 180.0, 240.0, 270.0, 330.0);
    for (int i = 0; i < 8; ++i) {
        float lo = c[i];
        float hi = (i < 7) ? c[i + 1] : 360.0;
        if (hue >= lo && hue < hi) {
            float t = (hue - lo) / (hi - lo);
            return b[i] * (1.0 - t) + b[(i + 1) & 7] * t;
        }
    }
    return b[0];
}

void main()
{
    vec4 c = texture(tex, v_texcoord);
    vec3 col = c.rgb;

    // Same order and math as the node apply() methods so preview predicts
    // export. 1. Global tone (TuneNode): white balance, then exposure/contrast/sat.
    // WB is a 3x3 applied in linear light (row-major in the UBO; mat3() takes
    // columns) — matches TuneNode::applyWhiteBalance.
    mat3 wbM = mat3(ubuf.wb00, ubuf.wb10, ubuf.wb20,
                    ubuf.wb01, ubuf.wb11, ubuf.wb21,
                    ubuf.wb02, ubuf.wb12, ubuf.wb22);
    vec3 wbLin = pow(max(col, 0.0), vec3(2.2));
    wbLin = wbM * wbLin;
    col = pow(max(wbLin, 0.0), vec3(1.0 / 2.2));
    col = applyTone(col, ubuf.exposure, ubuf.contrast, ubuf.saturation, ubuf.vibrance);
    // 2. Tone curves: each channel maps through its own LUT column (R->.r,
    //    G->.g, B->.b). Identity when no curve.
    col = vec3(texture(lut, vec2(col.r, 0.5)).r,
               texture(lut, vec2(col.g, 0.5)).g,
               texture(lut, vec2(col.b, 0.5)).b);
    // 2.5 Colour grade (Lift/Gamma/Gain = Slope/Offset/Power), encoded space —
    //     matches ColorGradeNode::apply().
    if (ubuf.gradeEnabled > 0.5) {
        vec3 slope = vec3(ubuf.gradeSlope0, ubuf.gradeSlope1, ubuf.gradeSlope2);
        vec3 offset = vec3(ubuf.gradeOffset0, ubuf.gradeOffset1, ubuf.gradeOffset2);
        vec3 power = vec3(ubuf.gradePower0, ubuf.gradePower1, ubuf.gradePower2);
        col = pow(max(col * slope + offset, vec3(0.0)), power);
    }
    // 3. Look: trilinear 3D LUT, blended with the pre-look colour by intensity.
    vec3 lutCol = texture(lut3d, clamp(col, 0.0, 1.0)).rgb;
    col = mix(col, lutCol, ubuf.lutIntensity);
    // 3.5 Monochrome: weighted desaturation to grey, then optional toning. Same
    //     math as MonoNode::apply().
    if (ubuf.monoEnabled > 0.5) {
        float g = clamp(dot(col, vec3(ubuf.monoR, ubuf.monoG, ubuf.monoB)), 0.0, 1.0);
        // Per-color mix: brighten/darken by hue, scaled by chroma (neutrals stay).
        float chroma = max(col.r, max(col.g, col.b)) - min(col.r, min(col.g, col.b));
        // 3.0 = MonoNode::kBandGain (keep in sync).
        g = clamp(g * (1.0 + monoBandShift(monoHue(col)) * chroma * 3.0), 0.0, 1.0);
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
            vec3 adj = applyTone(col, ubuf.selExposure, ubuf.selContrast, ubuf.selSaturation, 0.0);
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
