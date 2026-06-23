#version 440

layout(location = 0) in vec2 v_texcoord;
layout(location = 0) out vec4 fragColor;

layout(binding = 1) uniform sampler2D tex; // the composited offscreen result

void main()
{
    fragColor = texture(tex, v_texcoord);
}
