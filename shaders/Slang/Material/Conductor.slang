implementing Material;

import "../Utility/Constants";
import PBR;

public struct Conductor : IMaterial
{
    public struct BSDF : IBSDF
    {
        static bool isEffectivelySmooth(const float ax, const float ay)
        {
            return max(ax, ay) < k::eps;
        }

        static float lambda(const float3 w, const float ax, const float ay)
        {
            const float t2t = pbr::tan2Theta(w);
            if (t2t >= k::infty || t2t <= -k::infty)
            {
                return 0.;
            }

            const float a2 = pbr::sqr(ax * pbr::cosPhi(w)) + pbr::sqr(ay * pbr::sinPhi(w));

            return 0.5 * (sqrt(1. + a2 * t2t) - 1.);
        }

        static float G1(const float3 w, const float ax, const float ay)
        {
            return 1. / (1. + lambda(w, ax, ay));
        }

        static float G(const float3 wo, const float3 wi, const float ax, const float ay)
        {
            return 1. / (1. + lambda(wo, ax, ay) + lambda(wi, ax, ay));
        }

        static float D(const float3 wm, const float ax, const float ay)
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

        static float D(const float3 w, const float3 wm, const float ax, const float ay)
        {
            return G1(w, ax, ay) / abs(pbr::cosTheta(w)) * D(wm, ax, ay) * abs(dot(w, wm));
        }

        static float pdfGGX(const float3 w, const float3 wm, const float ax, const float ay)
        {
            return D(w, wm, ax, ay);
        }

        static float3 sampleWmGGX(const float3 w, const float2 u, const float ax, const float ay)
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

        public static Optional<BSDFSample> sample(MaterialParams params, BSDFContext ctx, SurfaceInteraction si, float sample1, float2 sample2)
        {
            BSDFSample ret;
            // glossy by default
            ret.flags = BSDFSample::Flags::Glossy | BSDFSample::Flags::Reflection;

            let relativeIOR = ctx.lastIOR / params.IOR;

            float ax = 0.5, ay = 0.5;
            pbr::calcAnisotropic(params.roughness, params.anisotropic, ax, ay);

            if (isEffectivelySmooth(ax, ay))
            {
                ret.flags = BSDFSample::Flags::Specular | BSDFSample::Flags::Reflection;
            }

            const float3 wm = sampleWmGGX(si.wi, sample2, ax, ay);
            const float3 wo = reflect(-si.wi, wm);
            if (wo.z * si.wi.z <= 0.)  // not in same hemisphere
            {
                return none;
            }

            ret.pdf = pdf(params, ctx, si, wo);
            ret.f   = eval(params, ctx, si, wo);
            ret.wo  = wo;

            return ret;
        }

        public static float pdf(MaterialParams params, BSDFContext ctx, SurfaceInteraction si, float3 wo)
        {
            if (wo.z * si.wi.z <= 0.)  // not in same hemisphere
            {
                return 0.;
            }

            float3 wm = normalize(wo + si.wi);
            if (dot(wm, wm) == 0.)
            {
                return 0.;
            }

            float ax = 0.5, ay = 0.5;
            pbr::calcAnisotropic(params.roughness, params.anisotropic, ax, ay);
            wm = faceforward(wm, wm, float3(0., 0., 1.));

            return pdfGGX(wo, wm, ax, ay) / (4. * abs(dot(wo, wm)));
        }

        public static float3 eval(MaterialParams params, BSDFContext ctx, SurfaceInteraction si, float3 wo)
        {
            let relativeIOR = ctx.lastIOR / params.IOR;
            float ax = 0.5, ay = 0.5;
            pbr::calcAnisotropic(params.roughness, params.anisotropic, ax, ay);

            // not in same hemisphere or smooth
            if (wo.z * si.wi.z <= 0. || isEffectivelySmooth(ax, ay))
            {
                return k::zeros.xyz;
            }

            let ct_o = abs(pbr::cosTheta(wo));
            let ct_i = abs(pbr::cosTheta(si.wi));
            if (ct_i == 0. || ct_o == 0.)
            {
                return k::zeros.xyz;
            }

            float3 wm = normalize(si.wi + wo);
            if (dot(wm, wm) == 0.)  // invalid microfacet normal
            {
                return k::zeros.xyz;
            }
            wm = normalize(wm);

            let F = pbr::disneyFresnel(params.albedo, params.specularTint, params.metallic, params.IOR, relativeIOR, si.wi, wm, wo);

            return F * D(wm, ax, ay) * G(wo, si.wi, ax, ay) / (4. * ct_i * ct_o);
        }
    }

}