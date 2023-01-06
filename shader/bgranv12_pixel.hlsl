//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

// Per-pixel color data passed through the pixel shader.
struct PixelShaderInput
{
    float4 pos         : SV_POSITION;
    float2 texCoord    : TEXCOORD0;
};

struct PS_OUTPUT
{
    float  p_y   : SV_Target0;
    float2 p_uv  : SV_Target1;
};

Texture2D<float4> bgraChannel        : t0;
SamplerState      defaultSampler     : s0;

// Derived from https://msdn.microsoft.com/en-us/library/windows/desktop/dd206750(v=vs.85).aspx
// Section: Converting 8-bit RGB888 to YUV BT.601
static const float3x3 RGBtoYUVCoeffMatrix =
{
        0.256788f,  -0.148223f,  0.439216f,
        0.504129f,  -0.290993f, -0.367788f,
        0.097906f,   0.439216f, -0.071427f
};

[numthreads (8,8,1)]
PS_OUTPUT PS(PixelShaderInput input) : SV_TARGET
{
        PS_OUTPUT output;
        if (input.texCoord.y <= 0.5f) {
            float4 bgra = bgraChannel.Sample(defaultSampler, float2(input.texCoord.x, input.texCoord.y * 2));

            float3 rgb = bgra.rgb;
            float3 yuv = mul(rgb, RGBtoYUVCoeffMatrix);
            yuv += float3(0.062745f, 0.501960f, 0.501960f);
            yuv = saturate(yuv);

            output.p_y = yuv.x;
            output.p_uv = yuv.yz;
        } else {
            float4 bgra = bgraChannel.Sample(defaultSampler, float2(input.texCoord.x, input.texCoord.y * 2 - 1));

            output.p_y = bgra.a;
            output.p_uv = float2(0.501960f, 0.501960f);
        }
        return output;
}
