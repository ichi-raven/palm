module GGX;

import "../Utility/Constants";
import "../Utility/Frame";
import "../Utility/Warp";

import PBR;

namespace ggx
{
    public float lambda(const float3 w, const float ax, const float ay)
    {
        const float t2t = pbr::tan2Theta(w);
        if (t2t >= k::infty || t2t <= -k::infty)
        {
            return 0.;
        }

        const float a2 = pbr::sqr(ax * pbr::cosPhi(w)) + pbr::sqr(ay * pbr::sinPhi(w));

        return 0.5 * (sqrt(1. + a2 * t2t) - 1.);
    }

    public float G1(const float3 w, const float ax, const float ay)
    {
        return 1. / (1. + lambda(w, ax, ay));
    }

    public float G(const float3 wo, const float3 wi, const float ax, const float ay)
    {
        return 1. / (1. + lambda(wo, ax, ay) + lambda(wi, ax, ay));
    }

    public float D(const float3 wm, const float ax, const float ay)
    {
        let t2t = pbr::tan2Theta(wm);
        if (t2t >= k::infty || t2t <= -k::infty)
        {
            return 0.;
        }

        let c4t = pbr::sqr(pbr::cos2Theta(wm));
        let e = t2t * (pbr::sqr(pbr::cosPhi(wm) / ax) + pbr::sqr(pbr::sinPhi(wm) / ay));

        return 1. / (k::pi * ax * ay * c4t * pbr::sqr(1. + e));
    }

    public float D(const float3 w, const float3 wm, const float ax, const float ay)
    {
        return G1(w, ax, ay) / abs(pbr::cosTheta(w)) * D(wm, ax, ay) * abs(dot(w, wm));
    }

    public float pdf(const float3 w, const float3 wm, const float ax, const float ay)
    {
        return D(w, wm, ax, ay);
    }

    public float3 sampleWm(const float3 w, const float2 u, const float ax, const float ay)
    {
        // transform w to hemispherical configuration
        float3 wh = normalize(float3(ax * w.x, ay * w.y, w.z));
        if (wh.z < 0.0)
        {
            wh = -wh;
        }

        // find ONB for visible normal sampling
        let vnFrame = Frame(wh);

        //generate uniformly distributed points on the unit disk
        float2 p = Warp::toUniformDiskPolar(u);

        // warp hemispherical projection for visible normal sampling
        const float h = sqrt(1. - pbr::sqr(p.x));
        p.y           = lerp(h, p.y, (1. + wh.z) / 2.);

        // reproject to hemisphere and transform normal to ellipsoid configuration
        const float pz = sqrt(max(0., 1. - dot(p, p)));
        const float3 nh  = vnFrame.toWorld(float3(p.x, p.y, pz));
        return normalize(float3(ax * nh.x, ay * nh.y, max(k::eps, nh.z)));
    }
}