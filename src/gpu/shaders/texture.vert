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
    float wb00;
    float wb01;
    float wb02;
    float wb10;
    float wb11;
    float wb12;
    float wb20;
    float wb21;
    float wb22;
    float gradeEnabled;
    float gradeSlope0;
    float gradeSlope1;
    float gradeSlope2;
    float gradeOffset0;
    float gradeOffset1;
    float gradeOffset2;
    float gradePower0;
    float gradePower1;
    float gradePower2;
    float selMaskOpacity;
    float vibrance; // consumed in the fragment stage
} ubuf;

void main()
{
    v_texcoord = texcoord;
    gl_Position = ubuf.mvp * vec4(position, 0.0, 1.0);
}
