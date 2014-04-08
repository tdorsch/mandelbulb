//--------------------------------------------------------------------------------------
// File: MandelboxPS.hlsl
//
// Ray marching implementation of mandelbox rendering.
//
//--------------------------------------------------------------------------------------
#include "frac.fx"


float DE(float3 p)
{
    const float scale = 9;
    const float3 boxfold = float3(1, 1, 1);
    const float spherefold = 0.2;
 
    float4 c0 = float4(p, 1);
    float4 c = c0;
    for (int i = 0; i < 4; ++i)
    {
        c.xyz = clamp(c.xyz, -boxfold, boxfold) * 2 - c.xyz;
        float rr = dot(c.xyz, c.xyz);
        c *= saturate(max(spherefold / rr, spherefold));
        c = c * scale + c0;
    } 
    return ((length(c.xyz) - (scale - 1)) / c.w - pow(scale, -3));
}

#include "raymarch.fx"

float4 shade(Ray ray)
{
    float4 rm = ray_marching(ray);
    if (rm.w < 0) return float4(0, 0, 0, 0);
    
    float3 p = rm.xyz;
    float k = DE(p);
    float gx = DE(p + float3(1e-5, 0, 0)) - k;
    float gy = DE(p + float3(0, 1e-5, 0)) - k;
    float gz = DE(p + float3(0, 0, 1e-5)) - k;
    float3 N = normalize(float3(gx, gy, gz));
    float3 L = normalize(float3(-1, 1, 2));

    float3 C = float3(0.5, 0.8, 0.9);
    float shadow = saturate(DE(p + L * 0.1) - k) / 0.1;
    float ao = 1 - rm.w / 128; ao = ao * ao;
    float A = 0.1;
    float3 col = (A + saturate(dot(L, N)) * shadow) * ao * C;

    return float4(col , 1);
}

float4 MandelboxPS( QuadVS_Output Input ) : SV_TARGET
{
    Ray ray;
    GetRay(Input.Tex, ray);

    float4 radiance = shade(ray);
  
    float3 col = float3(0.02, 0.02, 0.02);
    col = lerp(col, radiance.rgb, radiance.a);
    return float4(pow(col, 0.45), 1);
}


