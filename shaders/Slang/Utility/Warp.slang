module Warp;

import Constants;

public namespace Warp
{
    [ForceInline]
    public float toNormalDistribution(const float2 u)
    {
        // Box-Muller transform
        let sqrtLog = sqrt(-2. * log(u.x));
        let c       = cos(2. * k::pi * u.y);

        return sqrtLog * c;
    }

    [ForceInline]
    public float2 toNormalDistribution2D(const float2 u)
    {
        // Box-Muller transform
        let sqrtLog = sqrt(-2. * log(u.x));
        let c       = cos(2. * k::pi * u.y);
        let s       = sin(2. * k::pi * u.y);

        return sqrtLog * float2(c, s);
    }

    [ForceInline]
    public float3 toUniformSphere(float2 u)
    {
        let a = u.x * 2. * k::pi;
        let z = u.y * 2. - 1.;
        let r = sqrt(1. - z * z);
        return float3(r * cos(a), r * sin(a), z);
    }

    [ForceInline]
    public float3 toUniformHemiSphere(float2 u)
    {
        float3 ret = toUniformSphere(u);
        if (ret.z < 0.0)
        {
            ret.z *= -1.0;
        }

        return ret;
    }

    [ForceInline]
    public float2 toUniformDiskConcentric(float2 u)
    {
        let uOffset = 2. * u - float2(1., 1.);
        if (uOffset.x == 0 && uOffset.y == 0)
        {
            return float2(0., 0.);
        }

        float theta = 0, r = 0;
        if (abs(uOffset.x) > abs(uOffset.y))
        {
            r = uOffset.x;
            theta = k::piOver4 * (uOffset.y / uOffset.x);
        }
        else
        {
            r = uOffset.y;
            theta = k::piOver2 - k::piOver4 * (uOffset.x / uOffset.y);
        }

        return r * float2(cos(theta), sin(theta));
    }

    [ForceInline]
    public float2 toUniformDiskPolar(float2 u)
    {
        let r     = sqrt(u.x);
        let theta = 2. * k::pi * u.y;
        return float2(r * cos(theta), r * sin(theta));
    }

    [ForceInline]
    public float3 toCosineWeightedHemisphere(float2 u)
    {
        // pbrt implementation
        let d = toUniformDiskConcentric(u);
        let z = sqrt(max(1 - (d.x * d.x) - (d.y * d.y), 0.));
        return float3(d.x, d.y, z);
    }

    [ForceInline]
    public float heuristic<let beta : uint>(const float target, const float values[])
    {
        var denom = 0.0;
        var t     = target;

        //[unroll]
        for (int i = 0; i < values.getCount(); ++i)
        {
            var p = values[i];
            // [unroll]
            // for (int j = 1; j < beta; ++j)
            // {
            //     p *= values[i];
            // }
            denom += p;
        }

        // [unroll]
        // for (int j = 1; j < beta; ++j)
        // {
        //     t *= target;
        // }

        return t / denom;
    }
}