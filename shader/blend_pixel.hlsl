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

Texture2D<float4> textureChannel     : t0; 
SamplerState      defaultSampler     : s0;

[numthreads (8,8,1)]
float4 PS(PixelShaderInput input) : SV_TARGET
{
	float4 value = textureChannel.Sample(defaultSampler, input.texCoord);  
	return value;
}
