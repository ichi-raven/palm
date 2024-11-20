implementing Material;

import "../Utility/Constants";
import PBR;
import GGX;

public struct Conductor : IMaterial
{
    public struct BSDF : IBSDF
    {
        public static Optional<BSDFSample> sample(MaterialParams params, BSDFContext ctx, SurfaceInteraction si, float sample1, float2 sample2)
        {
            BSDFSample ret;
            // glossy by default
            ret.flags = BSDFSample::Flags::Glossy | BSDFSample::Flags::Reflection;

            let relativeIOR = ctx.lastIOR / params.IOR;

            float ax = 0.5, ay = 0.5;
            pbr::calcAnisotropic(params.roughness, params.anisotropic, ax, ay);

            if (pbr::isEffectivelySmooth(ax, ay))
            {
                ret.flags = BSDFSample::Flags::Specular | BSDFSample::Flags::Reflection;
                let wo = float3(-si.wi.x, -si.wi.y, si.wi.z);
                let absctwi = abs(pbr::cosTheta(wo));
                let F = pbr::disneyFresnel(params.albedo, params.specularTint, params.metallic, params.IOR, relativeIOR, si.wi, float3(0., 0., 1.), wo);
                
                ret.f = F / absctwi;
                ret.pdf = 1.0;// delta
                ret.wo = wo;
                return ret;
            }

            const float3 wm = ggx::sampleWm(si.wi, sample2, ax, ay);
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

            return ggx::pdf(si.wi, wm, ax, ay) / (4. * abs(dot(si.wi, wm)));
        }

        public static float3 eval(MaterialParams params, BSDFContext ctx, SurfaceInteraction si, float3 wo)
        {
            let relativeIOR = ctx.lastIOR / params.IOR;
            float ax = 0.5, ay = 0.5;
            pbr::calcAnisotropic(params.roughness, params.anisotropic, ax, ay);

            // not in same hemisphere or smooth
            if (wo.z * si.wi.z <= 0. || pbr::isEffectivelySmooth(ax, ay))
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

            return F * ggx::D(wm, ax, ay) * ggx::G(wo, si.wi, ax, ay) / (4. * ct_i * ct_o);
        }
    }

}