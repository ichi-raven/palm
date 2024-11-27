implementing Material;

import "../Utility/Constants";
import "../Utility/Warp";
import PBR;
import GGX;

__include Lambert;
__include Conductor;
__include Dielectric;

public struct Principle : IMaterial
{
    public struct BSDF : IBSDF
    {
        static float3 evaluateSheen(const float3 baseColor, const float sheen, const float3 sheenTint, const float3 wi, const float3 wm, const float3 wo)
        {
            if(sheen <= 0.0) 
            {
                return float3(0.0);
            }

            let dotHL = dot(wm, wo);
            let tint = pbr::calculateTint(baseColor);
            return sheen * lerp(float3(1.0), tint, sheenTint) * pbr::schlickWeight(dotHL);
        }

        static float evaluateDisneyRetroDiffuse(const float roughness, const float3 wi, const float3 wm, const float3 wo)
        {
            let dotNL = abs(pbr::cosTheta(wo));
            let dotNV = abs(pbr::cosTheta(wi));

            // square
            let alpha = roughness * roughness;

            let rr = 0.5 + 2.0 * dotNL * dotNL * alpha;
            let fl = pbr::schlickWeight(dotNL);
            let fv = pbr::schlickWeight(dotNV);

            return rr * (fl + fv + fl * fv * (rr - 1.0));
        }

        static float evaluateDisneyDiffuse(const float3 baseColor, const float flatness, const float roughness, const float3 wi, const float3 wm,
                                        const float3 wo)
        {
            let dotNL = abs(pbr::cosTheta(wo));
            let dotNV = abs(pbr::cosTheta(wi));

            let fl = pow((1.0 - dotNL), 5.0);
            let fv = pow((1.0 - dotNV), 5.0);

            float hanrahanKrueger = 0.0;

            if (flatness > 0.0)
            {
                let alpha = roughness * roughness;

                let dotHL = dot(wm, wo);
                let fss90 = dotHL * dotHL * alpha;
                let fss   = lerp(1.0, fss90, fl) * lerp(1.0, fss90, fv);

                hanrahanKrueger = 1.25 * (fss * (1.0 / (dotNL + dotNV) - 0.5) + 0.5);
            }

            let lambert = 1.0;
            // what
            let retro            = evaluateDisneyRetroDiffuse(roughness, wi, wm, wo);
            let subsurfaceApprox = lerp(lambert, hanrahanKrueger, flatness);

            return k::invPi * (retro + subsurfaceApprox * (1.0 - 0.5 * fl) * (1.0 - 0.5 * fv));
        }

        static Optional<BSDFSample> sampleDisneyDiffuse(const MaterialParams params, float3 wi,
                                        const float u, const float2 u2)
        {
            BSDFSample ret;

            float sign = pbr::sgn(pbr::cosTheta(wi));

            ret.wo    = sign * Warp::toCosineWeightedHemisphere(u2);
            float3 wm = normalize(wi + ret.wo);

            let dotNL = pbr::cosTheta(ret.wo);
            if (dotNL == 0.0)
            {
                return none;
            }

            float dotNV  = pbr::cosTheta(wi);
            float3 color = params.albedo;
            float pdf;
            if (u <= params.diffTrans)
            {
                wi  = -wi;
                pdf = params.diffTrans;

                color = sqrt(color);
            }
            else
            {
                pdf = (1.0 - params.diffTrans);
            }

            let sheen = evaluateSheen(params.albedo, params.sheen, params.sheenTint, wi, wm, ret.wo);

            let diffuse = evaluateDisneyDiffuse(params.albedo, params.flatness, params.roughness, wi, wm, ret.wo);

            ret.f     = sheen + color * diffuse;
            ret.pdf   = abs(dotNL) * pdf;
            ret.flags = BSDFSample::Flags::Diffuse | BSDFSample::Flags::Reflection;

            return ret;
        }

        [ForceInline]
        static void calcLobePdfs(const MaterialParams params,
                              out float pSpecular, out float pDiffuse, out float pClearcoat, out float pSpecTrans)
        {
            let metallicBRDF   = params.metallic;
            let specularBSDF   = (1.0 - params.metallic) * params.specTrans;
            let dielectricBRDF = (1.0 - params.specTrans) * (1.0 - params.metallic);

            let specularWeight = metallicBRDF + dielectricBRDF;
            let transmissionWeight = specularBSDF;
            let diffuseWeight      = dielectricBRDF;
            let clearcoatWeight    = clamp(params.clearcoat, 0.0, 1.0); 

            let norm = 1.0 / (specularWeight + transmissionWeight + diffuseWeight + clearcoatWeight);

            pSpecular  = specularWeight     * norm;
            pSpecTrans = transmissionWeight * norm;
            pDiffuse   = diffuseWeight      * norm;
            pClearcoat = clearcoatWeight    * norm;
        }

        public static Optional<BSDFSample> sample(MaterialParams params, BSDFContext ctx, SurfaceInteraction si, float sample1, float2 sample2)
        {
            float pSpecular = 0;
            float pDiffuse = 0;
            float pClearcoat = 0;
            float pTransmission = 0;
            calcLobePdfs(params, pSpecular, pDiffuse, pClearcoat, pTransmission);
            
            if (sample1 <= pSpecular) 
            {
                if (let res = Conductor.BSDF.sample(params, ctx, si, sample1, sample2))
                {
                    var ret = res;
                    ret.pdf *= pSpecular;
                    return ret;
                }
            }
            else if(sample1 > pSpecular && sample1 <= (pSpecular + pClearcoat)) 
            {
                MaterialParams clearcoatParams = params; // only for clearcoat
                clearcoatParams.roughness      = lerp(0.1, 0.001, params.clearcoatGloss);
                if (let res = Conductor.BSDF.sample(clearcoatParams, ctx, si, sample1, sample2))
                {
                    var ret = res;
                    ret.pdf *= pClearcoat;
                    return ret;
                }
            }
            else if(sample1 > pSpecular + pClearcoat && sample1 <= (pSpecular + pClearcoat + pDiffuse)) 
            {
                //if (let res = Lambert.BSDF.sample(params, ctx, si, sample1, sample2))
                if (let res = sampleDisneyDiffuse(params, si.wi, sample1, sample2))
                {
                    var ret = res;
                    ret.pdf *= pDiffuse;
                    return ret; 
                }
            }
            else if(pTransmission >= 0.0) 
            {
                if (let res = Dielectric.BSDF.sample(params, ctx, si, sample1, sample2))
                {
                    var ret = res;
                    ret.pdf *= pTransmission;
                    return ret; 
                }
            }

            // invalid lobe sampling
            return none;
        }

        public static float pdf(MaterialParams params, BSDFContext ctx, SurfaceInteraction si, float3 wo)
        {
            let wm = normalize(si.wi + wo);

            let dotNV = pbr::cosTheta(si.wi);
            let dotNL = pbr::cosTheta(wo);

            float pBRDF = 0.0, pDiffuse = 0.0, pClearcoat = 0.0, pSpecTrans = 0.0;
            calcLobePdfs(params, pBRDF, pDiffuse, pClearcoat, pSpecTrans);

            // calculate all of the anisotropic params
            float ax, ay;
            pbr::calcAnisotropic(params.roughness, params.anisotropic, ax, ay);

            let diffuseWeight = (1.0 - params.metallic) * (1.0 - params.specTrans);
            let transWeight   = (1.0 - params.metallic) * params.specTrans;

            // whole pdf
            float pdf = 0.0;

            // clearcoat
            let upperHemisphere = dotNL > 0.0 && dotNV > 0.0;
            if (upperHemisphere && params.clearcoat > 0.0)
            {
                MaterialParams clearcoatParams = params;  // only for clearcoat
                clearcoatParams.roughness = lerp(0.1, 0.001, params.roughness * params.roughness);
                pdf += pClearcoat * Conductor.BSDF.pdf(clearcoatParams, ctx, si, wo);
            }

            // diffuse
            if (diffuseWeight > 0.0)
            {
                //pdf += pDiffuse * Lambert.BSDF.pdf(params, ctx, si, wo);
                pdf += pDiffuse * abs(pbr::cosTheta(wo));
            }

            // transmission
            if (transWeight > 0.0)
            {
                pdf += pSpecTrans * Dielectric.BSDF.pdf(params, ctx, si, wo);
            }

            // specular
            if (upperHemisphere)
            {
                pdf += pBRDF * Conductor.BSDF.pdf(params, ctx, si, wo);
            }

            return saturate(pdf);
        }

        public static float3 eval(MaterialParams params, BSDFContext ctx, SurfaceInteraction si, float3 wo)
        {
            let wm = normalize(si.wi + wo);

            let dotNV = pbr::cosTheta(si.wi);
            let dotNL = pbr::cosTheta(wo);

            // whole reflectance
            float3 reflectance = float3(0.0);

            float pBRDF = 0.0, pDiffuse = 0.0, pClearcoat = 0.0, pSpecTrans = 0.0;
            calcLobePdfs(params, pBRDF, pDiffuse, pClearcoat, pSpecTrans);

            // calculate all of the anisotropic params
            float ax, ay;
            pbr::calcAnisotropic(params.roughness, params.anisotropic, ax, ay);

            let diffuseWeight = (1.0 - params.metallic) * (1.0 - params.specTrans);
            let transWeight   = (1.0 - params.metallic) * params.specTrans;

            // clearcoat
            bool upperHemisphere = dotNL > 0.0 && dotNV > 0.0;
            if(upperHemisphere && params.clearcoat > 0.0)
            {
                MaterialParams clearcoatParams = params;  // only for clearcoat
                clearcoatParams.roughness      = lerp(0.1, 0.001, params.roughness * params.roughness);
                let clearcoat                  = Conductor.BSDF.eval(clearcoatParams, ctx, si, wo);
                reflectance += clearcoat;
            }

            // diffuse
            if(diffuseWeight > 0.0) 
            {
                //let diffuse = Lambert.BSDF.eval(params, ctx, si, wo);
                let diffuse = evaluateDisneyDiffuse(params.albedo, params.flatness, params.roughness, si.wi, wm, wo);
                let sheen = evaluateSheen(params.albedo, params.sheen, params.sheenTint, si.wi, wm, wo);

                reflectance += diffuseWeight * (diffuse * params.albedo + sheen);
            }

            // transmission
            if(transWeight > 0.0) 
            {
                let transmission = Dielectric.BSDF.eval(params, ctx, si, wo);
                reflectance += transWeight * transmission;
            }

            // specular
            if(upperHemisphere) 
            {
                let specular = Conductor.BSDF.eval(params, ctx, si, wo);
                reflectance += specular;
            }

            return reflectance;
        }
    }

}