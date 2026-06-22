#version 440

layout(location = 0) in vec2 v_texcoord;

layout(location = 0) out vec4 fragColor;

layout(binding = 1) uniform sampler2D tex;

layout(std140, binding = 0) uniform buf {
    mat4 mvp;
    float exposure;   // EV stops
    float contrast;   // factor, 1 = neutral
    float saturation; // factor, 1 = neutral
} ubuf;

void main()
{
    vec4 c = texture(tex, v_texcoord);
    vec3 col = c.rgb;

    // Same order and math as TuneNode::apply() so preview predicts export.
    // 1. Exposure: encoded-space multiply (power-law gamma 2.2).
    col *= exp2(ubuf.exposure / 2.2);
    // 2. Contrast: scale around mid-grey.
    col = (col - 0.5) * ubuf.contrast + 0.5;
    // 3. Saturation: mix toward Rec.709 luma.
    float luma = dot(col, vec3(0.2126, 0.7152, 0.0722));
    col = mix(vec3(luma), col, ubuf.saturation);

    fragColor = vec4(col, c.a);
}
