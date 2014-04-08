//--------------------------------------------------------------------------------------
// File: MandelboxPS.hlsl
//
// Ray marching implementation of mandelbox rendering.
//
//--------------------------------------------------------------------------------------
#include "frac.fx"


float DE(float3 p)
{
  float3 c = p;
  float r = length(c);
  float dr = 1;
  for (int i = 0; i < 4 && r < 3; ++i)
  {
    float xr = pow(r, 7);
    dr = 6 * xr * dr + 1;
  
    float theta = atan2(c.y, c.x) * 8;
    float phi = asin(c.z / r) * 8;
    r = xr * r;
    c = r * float3(cos(phi) * cos(theta), cos(phi) * sin(theta), sin(phi));
   
    c += p;
    r = length(c);
  }
  return 0.35 * log(r) * r / dr;
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
  
  float ao = 0;
  ao += DE(p + 0.1 * N) * 2.5;
  ao += DE(p + 0.2 * N) * 1.0;

  float3 L = normalize(float3(-1, 1, 2));
  ray.pos = p + N * 0.01;
  ray.dir = L;
  float4 S = ray_marching(ray);
  float3 C = lerp(float3(0.6, 0.8, 0.6), float3(1.0, 0.0, 0.0), rm.w / 64);
  float D = 0.7 * (S.w < 0 ? 1 : 0);
  
  float A = 0.1;
  float3 col = (A + D * saturate(dot(L, N))) * ao * C;
  return float4(col , 1);
}


float4 MandelbulbPS( QuadVS_Output Input ) : SV_TARGET
{
    Ray ray;
    GetRay(Input.Tex, ray);

    float4 radiance = shade(ray);
  
    float3 col = float3(0.02, 0.02, 0.02);
    col = lerp(col, radiance.rgb, radiance.a);
    return float4(pow(col, 0.45), 1);
}
