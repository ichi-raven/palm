module Warp;

import Constants;

public namespace Warp
{
    public float3 toUniformSphere(float2 u)
    {
        float a = u.x * k::pi2;
        float z = u.y * 2. - 1.;
        float r = sqrt(1 - z * z);
        return float3(r * cos(a), r * sin(a), z);
    }

    public float3 toUniformHemiSphere(float2 u)
    {
        float3 ret = toUniformSphere(u);
        if (ret.z < 0.0)
        {
            ret.z *= -1.0;
        }

        return ret;
    }

    public float3 toCosineWeightedHemisphere(float2 u)
    {
        const float r2Sqrt = sqrt(u.y);
        const float z      = sqrt(1 - u.y);
        const float phi    = k::pi2 * u.x;

        return float3(cos(phi) * r2Sqrt, sin(phi) * r2Sqrt, z);
    }
}