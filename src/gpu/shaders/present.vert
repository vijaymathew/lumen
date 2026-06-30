#version 440

// Present pass: draws a textured quad to the screen with the zoom/pan transform.
// (The adjustment chain has already run into an offscreen texture.)

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 texcoord;

layout(location = 0) out vec2 v_texcoord;
layout(location = 1) out vec2 v_imagecoord; // 0..1 over the displayed cropped frame

layout(std140, binding = 0) uniform buf {
    mat4 mvp;
    mat4 texXform; // output unit-quad → source texcoord (crop + orientation)
    vec4 vig0;     // creative vignette: amount, midpoint, roundness, feather
    vec4 vig1;     // aspect (W/H), enabled, pad, pad
} ubuf;

void main()
{
    v_texcoord = (ubuf.texXform * vec4(texcoord, 0.0, 1.0)).xy;
    v_imagecoord = texcoord; // pre-crop-transform: positions the vignette on the frame
    gl_Position = ubuf.mvp * vec4(position, 0.0, 1.0);
}
