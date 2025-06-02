implementing Material;

import "../Utility/Constants";
import "../Utility/Warp";
import "../Sampler/Sampler";
import PBR;
import GGX;

__include Lambert;
__include Conductor;
__include Dielectric;

public struct Principle : IMaterial
{
    public struct BSDF : IBSDF
    {
        // static float3 evaluateSheen(const float3 baseColor, const float sheen, const float3 sheenTint, const float3 wi, const float3 wm, const float3 wo)
        // {
        //     if(sheen <= 0.0) 
        //     {
        //         return float3(0.0);
        //     }

        //     let dotHL = abs(dot(wm, wo));
        //     let tint = pbr::calculateTint(baseColor);
        //     return sheen * lerp(float3(1.0), tint, sheenTint) * pbr::schlickWeight(dotHL);
        // }

        // static float evaluateDisneyRetroDiffuse(const float roughness, const float3 wi, const float3 wm, const float3 wo)
        // {
        //     let dotNL = abs(pbr::cosTheta(wo));
        //     let dotNV = abs(pbr::cosTheta(wi));

        //     let rr = 2.0 * dotNL * dotNL * roughness;
        //     let fl = pbr::schlickWeight(dotNL);
        //     let fv = pbr::schlickWeight(dotNV);

        //     return rr * (fl + fv + fl * fv * (rr - 1.0));
        // }

        // static float evaluateDisneyDiffuse(const float3 baseColor, const float flatness, const float roughness, const float3 wi, const float3 wm,
        //                                 const float3 wo)
        // {
        //     let dotNL = abs(pbr::cosTheta(wo));
        //     let dotNV = abs(pbr::cosTheta(wi));

        //     let fl = pow((1.0 - dotNL), 5.0);
        //     let fv = pow((1.0 - dotNV), 5.0);

        //     float hanrahanKrueger = 0.0;

        //     if (flatness > 0.0)
        //     {
        //         let alpha = roughness * roughness;

        //         let dotHL = dot(wm, wo);
        //         let fss90 = dotHL * dotHL * alpha;
        //         let fss   = lerp(1.0, fss90, fl) * lerp(1.0, fss90, fv);

        //         hanrahanKrueger = 1.25 * (fss * (1.0 / (dotNL + dotNV) - 0.5) + 0.5);
        //     }

        //     let lambert          = 1.0;
        //     let retro            = evaluateDisneyRetroDiffuse(roughness, wi, wm, wo);
        //     let subsurfaceApprox = lerp(lambert, hanrahanKrueger, flatness);

        //     return k::invPi * (retro + subsurfaceApprox * (1.0 - 0.5 * fl) * (1.0 - 0.5 * fv));
        // }

        // static Optional<BSDFSample> sampleDisneyDiffuse(const MaterialParams params, float3 wi,
        //                                 const float u, const float2 u2)
        // {
        //     BSDFSample ret;

        //     let sign = pbr::sgn(pbr::cosTheta(wi));

        //     ret.wo    = sign * Warp::toCosineWeightedHemisphere(u2);
        //     float3 wm = normalize(wi + ret.wo);

        //     let dotNL = pbr::cosTheta(ret.wo);
        //     if (dotNL == 0.0)
        //     {
        //         return none;
        //     }

        //     float dotNV  = pbr::cosTheta(wi);
        //     float3 color = params.albedo;
        //     float pdf = 1.0;

        //     let sheen = evaluateSheen(params.albedo, params.sheen, params.sheenTint, wi, wm, ret.wo);

        //     let diffuse = evaluateDisneyDiffuse(params.albedo, params.flatness, params.roughness, wi, wm, ret.wo);

        //     ret.f     = sheen + color * (diffuse / pdf);
        //     ret.pdf   = abs(dotNL) * pdf;
        //     ret.flags = BSDFSample::Flags::Diffuse | BSDFSample::Flags::Reflection;

        //     return ret;
        // }

        [ForceInline]
        static void calcLobePdfs(const MaterialParams params,
                              out float pSpecular, out float pDiffuse, out float pClearcoat, out float pSpecTrans)
        {
            let specTrans = params.specTrans * (1.0 - params.metallic);

            let specularWeight      = (1.0 - specTrans);
            let transmissionWeight  = specTrans;
            let diffuseWeight       = (1.0 - params.metallic) * (1.0 - params.specTrans);
            let clearcoatWeight     = saturate(params.clearcoat);

            let norm = 1.0 / (specularWeight + transmissionWeight + diffuseWeight + clearcoatWeight);

            pSpecular  = specularWeight * norm;
            pSpecTrans = transmissionWeight * norm;
            pDiffuse   = diffuseWeight * norm;
            pClearcoat = clearcoatWeight * norm;
        }

        public static Optional<BSDFSample> sample<S : ISampler>(MaterialParams params, BSDFContext ctx, SurfaceInteraction si, inout S sampler)
        {
            let lobeSample = sampler.next1D();

            float pSpecular = 0;
            float pDiffuse = 0;
            float pClearcoat = 0;
            float pTransmission = 0;
            calcLobePdfs(params, pSpecular, pDiffuse, pClearcoat, pTransmission);

            if (lobeSample <= pSpecular)  // specular
            {
                if (let res = Conductor.BSDF.sample(params, ctx, si, sampler)) 
                {
                    var ret = res;
                    //ret.f = Principle.BSDF.eval(params, ctx, si, ret.wo);
                    //ret.pdf = Principle.BSDF.pdf(params, ctx, si, ret.wo);
                    return ret;
                }
            }
            else if (lobeSample > pSpecular && lobeSample <= (pSpecular + pClearcoat)) // clearcoat
            {
                MaterialParams clearcoatParams    = params;  // only for clearcoat
                clearcoatParams.metallic          = 1.0;//
                clearcoatParams.roughness         = lerp(0.1, 0.001, params.clearcoatGloss);
                clearcoatParams.metalnessTexIndex = k::invalidTexIndex;
                clearcoatParams.albedo            = k::white;
                if (let res = Conductor.BSDF.sample(clearcoatParams, ctx, si, sampler))
                {
                    var ret = res;
                    //ret.f   = Principle.BSDF.eval(clearcoatParams, ctx, si, ret.wo);
                    //ret.pdf = Principle.BSDF.pdf(clearcoatParams, ctx, si, ret.wo);
                    return ret;
                }
            }
            else if (lobeSample > (pSpecular + pClearcoat) && lobeSample <= (pSpecular + pClearcoat + pDiffuse)) // diffuse
            {
                if (let res = Lambert.BSDF.sample(params, ctx, si, sampler))
                {
                    var ret = res;
                    //ret.f   = Principle.BSDF.eval(params, ctx, si, ret.wo);
                    //ret.pdf = Principle.BSDF.pdf(params, ctx, si, ret.wo);
                    return ret;                
                }
            }
            else if(pTransmission > 0.0) // transmission
            {
                if (let res = Dielectric.BSDF.sample(params, ctx, si, sampler))
                {
                    var ret = res;
                    //ret.f   = Principle.BSDF.eval(params, ctx, si, ret.wo);
                    //ret.pdf = Principle.BSDF.pdf(params, ctx, si, ret.wo);
                    return ret;
                }
            }

            // invalid sample
            return none;
        }

        public static float pdf(MaterialParams params, BSDFContext ctx, SurfaceInteraction si, float3 wo)
        {
            let dotNV = pbr::cosTheta(si.wi);
            let dotNL = pbr::cosTheta(wo);
            let upperHemisphere = dotNL > 0.0 && dotNV > 0.0;

            float specularWeight = 0.0, diffuseWeight = 0.0, clearcoatWeight = 0.0, transmissionWeight = 0.0;
            calcLobePdfs(params, specularWeight, diffuseWeight, clearcoatWeight, transmissionWeight);
            
            float pBRDF = 0.0, pDiffuse = 0.0, pClearcoat = 0.0, pSpecTrans = 0.0;

            // clearcoat
            if (upperHemisphere)
            {
                MaterialParams clearcoatParams    = {};  // only for clearcoat
                clearcoatParams.roughness         = lerp(0.1, 0.001, params.clearcoatGloss);
                clearcoatParams.metalnessTexIndex = k::invalidTexIndex;
                clearcoatParams.albedo            = k::white;
                pClearcoat = Conductor.BSDF.pdf(clearcoatParams, ctx, si, wo);
            }

            // diffuse
            {
                pDiffuse = Lambert.BSDF.pdf(params, ctx, si, wo);
            }

            // transmission
            {
                pSpecTrans = Dielectric.BSDF.pdf(params, ctx, si, wo);
            }

            // specular
            if (upperHemisphere)
            {
                pBRDF = Conductor.BSDF.pdf(params, ctx, si, wo);
            }

            return specularWeight * pBRDF + diffuseWeight * pDiffuse + clearcoatWeight * pClearcoat + transmissionWeight * pSpecTrans;
        }

        public static float3 eval(MaterialParams params, BSDFContext ctx, SurfaceInteraction si, float3 wo)
        {
            let dotNV = pbr::cosTheta(si.wi);
            let dotNL = pbr::cosTheta(wo);
            let upperHemisphere = dotNL > 0.0 && dotNV > 0.0;

            // whole reflectance
            float3 reflectance = float3(0.0);

            // weights for each lobe
            float specularWeight = 1.0, diffuseWeight = 1.0, clearcoatWeight = 1.0, transmissionWeight = 1.0;
            calcLobePdfs(params, specularWeight, diffuseWeight, clearcoatWeight, transmissionWeight);

            // clearcoat
            if (upperHemisphere)
            {
                MaterialParams clearcoatParams    = {};  // only for clearcoat
                clearcoatParams.roughness         = lerp(0.1, 0.001, params.clearcoatGloss);
                clearcoatParams.metalnessTexIndex = k::invalidTexIndex;
                clearcoatParams.albedo            = k::white;
                let clearcoat                  = Conductor.BSDF.eval(clearcoatParams, ctx, si, wo);
                reflectance += clearcoatWeight * clearcoat;
            }

            // diffuse
            if (upperHemisphere)
            {
                let diffuse = Lambert.BSDF.eval(params, ctx, si, wo);
                reflectance += diffuseWeight * diffuse;
            }

            // transmission
            {
                let transmission = Dielectric.BSDF.eval(params, ctx, si, wo);
                reflectance += transmissionWeight * transmission;
            }

            // specular
            {
                let specular = Conductor.BSDF.eval(params, ctx, si, wo);
                reflectance += specularWeight * specular;
            }

            return reflectance;
        }
    }

}