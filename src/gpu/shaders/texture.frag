#version 440

layout(location = 0) in vec2 v_texcoord;

layout(location = 0) out vec4 fragColor;

layout(binding = 1) uniform sampler2D tex;

layout(std140, binding = 0) uniform buf {
    mat4 mvp;
    float exposure; // EV stops
} ubuf;

void main()
{
    vec4 c = texture(tex, v_texcoord);
    // Linear-light exposure expressed as an encoded-space multiply (power-law
    // gamma 2.2), matching TuneNode::apply() so preview predicts export.
    float f = exp2(ubuf.exposure / 2.2);
    fragColor = vec4(c.rgb * f, c.a);
}
