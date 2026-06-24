#version 440

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 texcoord;

layout(location = 0) out vec2 v_texcoord;

layout(std140, binding = 0) uniform buf {
    mat4 mvp;
    float exposure;   // consumed in the fragment stage
    float contrast;
    float saturation;
    float lutIntensity;
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
    float layerOpacity;
    float monoEnabled; // all consumed in the fragment stage
    float monoR;
    float monoG;
    float monoB;
    float monoToneStrength;
    float monoToneR;
    float monoToneG;
    float monoToneB;
} ubuf;

void main()
{
    v_texcoord = texcoord;
    gl_Position = ubuf.mvp * vec4(position, 0.0, 1.0);
}
