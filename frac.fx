//--------------------------------------------------------------------------------------
// File: frac.fx
//
// Common functions and structs for ray marching implementation of mandelbulb rendering.
//
//--------------------------------------------------------------------------------------

cbuffer cbView : register( b0 )
{
    row_major matrix mProj;                   // projection matrix
    row_major matrix mInvWorld;               // inverse world matrix
    row_major matrix mInvView;                // inverse view matrix
    float dist;
    float pad0;
    float pad1;
    float pad2;
}

struct QuadVS_Output
{
    float4 Pos : SV_POSITION;
    float2 Tex : TEXCOORD0;
};

struct Ray 
{
    float3 pos;
    float3 dir;
};

Texture2D s0 : register(t0);

SamplerState PointSampler : register (s0);
SamplerState LinearSampler : register (s1);

void GetRay(in float2 pt, out Ray ray)
{
    float u = (pt.x * 2 - 1) / mProj[0][0];
    float v = (-pt.y * 2 + 1) / mProj[1][1];

    ray.pos = float3(mInvView[3][0], mInvView[3][1], mInvView[3][2]);
    ray.pos = (float3)mul(float4(ray.pos, 1), mInvWorld);

    ray.dir = normalize(float3(u, v, 1.0f));
    ray.dir = mul(ray.dir, (float3x3)mInvView);
    ray.dir = normalize(mul(ray.dir, (float3x3)mInvWorld));
}
