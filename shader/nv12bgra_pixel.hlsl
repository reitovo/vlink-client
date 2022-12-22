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

Texture2D<float>  luminanceChannel   : t0;
Texture2D<float2> chrominanceChannel : t1;
Texture2D<float>  alphaChannel       : t2;
SamplerState      defaultSampler     : s0;

// Derived from https://msdn.microsoft.com/en-us/library/windows/desktop/dd206750(v=vs.85).aspx
// Section: Converting 8-bit YUV to RGB888
static const float3x3 YUVtoRGBCoeffMatrix =
{
	1.164383f,  1.164383f, 1.164383f,
	0.000000f, -0.391762f, 2.017232f,
	1.596027f, -0.812968f, 0.000000f
};

float3 ConvertYUVtoBGR(float3 yuv)
{
	// Derived from https://msdn.microsoft.com/en-us/library/windows/desktop/dd206750(v=vs.85).aspx
	// Section: Converting 8-bit YUV to BGR888

	// These values are calculated from (16 / 255) and (128 / 255)
	yuv -= float3(0.062745f, 0.501960f, 0.501960f);
	yuv = mul(yuv, YUVtoRGBCoeffMatrix);
	yuv = saturate(yuv);//BGR
	return float3(yuv.x, yuv.y, yuv.z);
}

[numthreads (8,8,1)]
float4 PS(PixelShaderInput input) : SV_TARGET
{
	float y = luminanceChannel.Sample(defaultSampler, input.texCoord);
	float2 uv = chrominanceChannel.Sample(defaultSampler, input.texCoord);
        float a = alphaChannel.Sample(defaultSampler, input.texCoord);

	return float4(ConvertYUVtoBGR(float3(y, uv)), a);
}
