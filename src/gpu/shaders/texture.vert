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
    float monoBalance;
    float monoHighR;
    float monoHighG;
    float monoHighB;
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
    float monoBand0; // per-color B&W mix bands (consumed in the fragment stage)
    float monoBand1;
    float monoBand2;
    float monoBand3;
    float monoBand4;
    float monoBand5;
    float monoBand6;
    float monoBand7;
    float monoShadowR; // split-tone shadow tint
    float monoShadowG;
    float monoShadowB;
    float grainAmount; // consumed in the fragment stage
    float grainSize;
    float highlights;  // tonal-region amounts, consumed in the fragment stage
    float shadows;
    float whites;
    float blacks;
    float colorMixEnabled; // per-color HSL, all consumed in the fragment stage
    float mixHue0;
    float mixHue1;
    float mixHue2;
    float mixHue3;
    float mixHue4;
    float mixHue5;
    float mixHue6;
    float mixHue7;
    float mixSat0;
    float mixSat1;
    float mixSat2;
    float mixSat3;
    float mixSat4;
    float mixSat5;
    float mixSat6;
    float mixSat7;
    float mixLum0;
    float mixLum1;
    float mixLum2;
    float mixLum3;
    float mixLum4;
    float mixLum5;
    float mixLum6;
    float mixLum7;
} ubuf;

void main()
{
    v_texcoord = texcoord;
    gl_Position = ubuf.mvp * vec4(position, 0.0, 1.0);
}
