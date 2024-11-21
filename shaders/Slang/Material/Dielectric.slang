implementing Material;

import "../Utility/Constants";
import PBR;
import GGX;

public struct Dielectric : IMaterial
{
    public struct BSDF : IBSDF
    {
        public static Optional<BSDFSample> sample(MaterialParams params, BSDFContext ctx, SurfaceInteraction si, float sample1, float2 sample2)
        {
            BSDFSample ret;
            ret.flags = BSDFSample::Flags::Specular | BSDFSample::Flags::Reflection;

            float ax, ay;
            pbr::calcAnisotropic(params.roughness, params.anisotropic, ax, ay);

            // specular
            if (params.IOR == 1.0 || pbr::isEffectivelySmooth(ax, ay)) // specular
            {
                let pR = pbr::fresnelDielectric(pbr::cosTheta(si.wi), params.IOR, params.IOR / ctx.lastIOR);
                let pT = 1.0 - pR;

                if (sample1 < pR / (pR + pT)) // reflection
                {
                    // sample perfect specular BRDF
                    ret.flags = BSDFSample::Flags::Specular | BSDFSample::Flags::Reflection;
                    ret.wo = float3(-si.wi.x, -si.wi.y, si.wi.z);
                    ret.f = float3(pR / abs(pbr::cosTheta(ret.wo)));
                    ret.pdf = pR / (pR + pT);
                }
                else // refraction (transmission)
                {
                    // sample perfect specular BTDF
                    ret.flags = BSDFSample::Flags::Specular | BSDFSample::Flags::Transmission;
                    float3 wo;
                    float etap;
                    let valid = pbr::refract(si.wi, float3(0., 0., 1.), params.IOR, etap, wo);
                    if (!valid)
                    {
                        return none;
                    }

                    ret.f = float3(pT / abs(pbr::cosTheta(wo)));
                    ret.pdf = pT / (pR + pT);
                    ret.f /= pbr::sqr(etap); // if radiance
                    ret.wo = wo;
                }

                // early return
                // specular f and pdf can't calculate by simple eval
                return ret; 
            }
            
            // glossy 
            let wm = ggx::sampleWm(si.wi, sample2, ax, ay);
            let pR = pbr::fresnelDielectric(dot(si.wi, wm), params.IOR, params.IOR / ctx.lastIOR);
            let pT = 1. - pR;

            if (sample1 < pR / (pR + pT)) // reflected
            {
                ret.flags = BSDFSample::Flags::Glossy | BSDFSample::Flags::Reflection;
                let wo = reflect(-si.wi, wm);
                if (wo.z * si.wi.z <= 0.) // not in same hemisphere
                {
                    return none;
                }

                let ct_o = pbr::cosTheta(wo);
                let ct_i = pbr::cosTheta(si.wi);
                ret.pdf = ggx::pdf(si.wi, wm, ax, ay) / (4. * abs(dot(si.wi, wm))) * pR / (pR + pT);
                ret.f = float3(ggx::D(wm, ax, ay) * ggx::G(wo, si.wi, ax, ay) * pR / (4. * ct_i * ct_o));
                ret.wo = wo;
            }
            else // transmitted
            {
                ret.flags = BSDFSample::Flags::Glossy | BSDFSample::Flags::Transmission;
                float3 wo;
                float etap;
                let tir = !pbr::refract(si.wi, wm, params.IOR, etap, wo);

                if (wo.z * si.wi.z > 0. || wo.z == 0. || tir) // in same hemisphere or failed to refract
                {
                    return none;
                }

                let ct_o = pbr::cosTheta(wo);
                let ct_i = pbr::cosTheta(si.wi);
                let denom = pbr::sqr(dot(wo, wm) + dot(si.wi, wm) / etap);
                let dwm_dwo = abs(dot(wo, wm)) / denom;
                ret.pdf = ggx::pdf(si.wi, wm, ax, ay) * dwm_dwo * pT / (pR + pT);
                ret.f = float3(pT * ggx::D(wm, ax, ay) * ggx::G(wo, si.wi, ax, ay) * abs(dot(si.wi, wm) * dot(wo, wm) / (ct_i * ct_o * denom)));
                ret.wo = wo;
            }
            
            return ret;
        }

        public static float pdf(MaterialParams params, BSDFContext ctx, SurfaceInteraction si, float3 wo)
        {            
            float ax, ay;
            pbr::calcAnisotropic(params.roughness, params.anisotropic, ax, ay);

            if (params.IOR == 1.0 || pbr::isEffectivelySmooth(ax, ay))
            {
                return 0.;
            }
            
            let ct_o = pbr::cosTheta(wo);
            let ct_i = pbr::cosTheta(si.wi);

            let refl = ct_i * ct_o > 0.;
            float etap = 1.;

            if (!refl)
            {
                etap = ct_o > 0. ? params.IOR : (1. / params.IOR);
            }

            // compute generalized half vector wm
            float3 wm = wo * etap + si.wi;
            if (ct_i == 0. || ct_o == 0. || dot(wm, wm) == 0.)
            {
                return 0.;
            }

            wm = faceforward(wm, wm, float3(0., 0., 1.));

            // discard backfacing microfacets
            if (dot(wm, si.wi) * ct_i < 0. || dot(wm, wo) * ct_o < 0.)
            {
                return 0.;
            }

            let pR = pbr::fresnelDielectric(dot(si.wi, wm), params.IOR, params.IOR / ctx.lastIOR);
            let pT = 1. - pR;
            
            float pdf = 0.0;
            if (refl)
            {
                pdf = ggx::pdf(si.wi, wm, ax, ay) / (4. * abs(dot(si.wi, wm))) * pR / (pR + pT);
            }
            else
            {
                let denom = pbr::sqr(dot(wo, wm) + dot(si.wi, wm) / etap) * ct_i * ct_o;
                let dwm_dwo = abs(dot(wo, wm)) / denom;
                pdf = ggx::pdf(si.wi, wm, ax, ay) * dwm_dwo * pT / (pR + pT);
            }

            return pdf;
        }

        public static float3 eval(MaterialParams params, BSDFContext ctx, SurfaceInteraction si, float3 wo)
        {
            float ax, ay;
            pbr::calcAnisotropic(params.roughness, params.anisotropic, ax, ay);

            if (params.IOR == 1.0 || pbr::isEffectivelySmooth(ax, ay))
            {
                return k::zeros.xyz;
            }

            let ct_o = pbr::cosTheta(wo);
            let ct_i = pbr::cosTheta(si.wi);

            let refl = ct_i * ct_o > 0.;
            float etap = 1.;

            if (!refl)
            {
                etap = ct_o > 0. ? params.IOR : (1. / params.IOR);
            }

            float3 wm = wo * etap + si.wi;
            if (ct_i == 0. || ct_o == 0. || dot(wm, wm) == 0.)
            {
                return k::zeros.xyz;
            }

            wm = faceforward(wm, wm, float3(0., 0., 1.));

            // discard backfacing microfacets
            if (dot(wm, si.wi) * ct_i < 0. || dot(wm, wo) * ct_o < 0.)
            {
                return k::zeros.xyz;
            }

            let F = pbr::fresnelDielectric(dot(si.wi, wm), params.IOR, params.IOR / ctx.lastIOR);

            if (refl)
            {
                return float3(ggx::D(wm, ax, ay) * F * ggx::G(wo, si.wi, ax, ay) / (4. * ct_i * ct_o));
            }
            else
            {
                let denom = pbr::sqr(dot(wo, wm) + dot(si.wi, wm) / etap) * ct_i * ct_o;
                let dwm_dwo = abs(dot(wo, wm)) / denom;
                return float3((1. - F) * ggx::D(wm, ax, ay) * ggx::G(wo, si.wi, ax, ay)* abs(dot(si.wi, wm)) * abs(dot(wo, wm)) / denom);
            }

            return k::zeros.xyz;
        }
    }
}