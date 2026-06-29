#version 440

// Present pass: draws a textured quad to the screen with the zoom/pan transform.
// (The adjustment chain has already run into an offscreen texture.)

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 texcoord;

layout(location = 0) out vec2 v_texcoord;

layout(std140, binding = 0) uniform buf {
    mat4 mvp;
    mat4 texXform; // output unit-quad → source texcoord (crop + orientation)
} ubuf;

void main()
{
    v_texcoord = (ubuf.texXform * vec4(texcoord, 0.0, 1.0)).xy;
    gl_Position = ubuf.mvp * vec4(position, 0.0, 1.0);
}
