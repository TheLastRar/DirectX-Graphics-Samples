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

struct PSInput
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
};

PSInput VSMain(float4 position : POSITION, float4 color : COLOR)
{
    PSInput result;

    result.position = position;
    result.color = color;

    return result;
}

struct PSOutput
{
    float4 color0 : SV_TARGET0;
    float color1 : SV_TARGET1;
#if DISABLE_EARLY_Z == 1
    float depth : SV_DEPTH;
#endif
};

PSOutput PSMain(PSInput input)
{
    PSOutput output;
    output.color0 = input.color;
    output.color1 = input.position.z;
#if DISABLE_EARLY_Z == 1
    output.depth = input.position.z;
#endif
    return output;
}

float4 VSMainCompare(float4 position : POSITION) : SV_POSITION
{
    return position;
}

texture2D<float> tex0 : register(t0);
texture2D<float> tex1 : register(t1);

float4 PSMainCompare(float4 position : SV_POSITION) : SV_TARGET0
{
    float val0 = tex0.Load(int3(int2(position.xy), 0));
    float val1 = tex1.Load(int3(int2(position.xy), 0));
    return val0 == val1 ? float4(0, 1, 0, 1) : float4(1, 0, 0, 1);
}
