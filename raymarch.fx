//--------------------------------------------------------------------------------------
// File: raymarch.fx
//
// Ray marching routines for mandelbulb rendering.
//
//--------------------------------------------------------------------------------------

float4 ray_marching(Ray ray)
{
    for (int i = 0; i < 128; ++i)
    {
        float d = DE(ray.pos);
        ray.pos += d * ray.dir;
        if (d < (dist * dist * 0.0001)) return float4(ray.pos, i);
    }
    return float4(ray.pos, -1);
}
