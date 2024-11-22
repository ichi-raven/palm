implementing Material;

import "../Utility/Constants";
import "../Utility/Warp";
import PBR;

__include Lambert;
__include Conductor;
__include Dielectric;

public struct Principle : IMaterial
{
    public struct BSDF : IBSDF
    {
        static float evaluateDisneyClearcoat(const float clearcoat, const float alpha, const float3 wi, const float3 wm,
                                     const float3 wo)
        {
            if(clearcoat <= 0.0) 
            {
                return 0.0;
            }

            let absDotNH = abs(pbr::cosTheta(wm));
            let absDotNL = abs(pbr::cosTheta(wo));
            let absDotNV = abs(pbr::cosTheta(wi));
            let dotHL = dot(wm, wo);

            let d = pbr::D_GTR1(absDotNH, lerp(0.1, 0.001, alpha));
            let f = pbr::schlick(0.04, dotHL);
            let gl = pbr::separableSmithGGXG1(wo, 0.25);
            let gv = pbr::separableSmithGGXG1(wi, 0.25);

            return 0.25 * clearcoat * d * f * gl * gv;
        }

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

        static Optional<BSDFSample> sampleDisneyClearcoat(const MaterialParams params, const float3 wi,
                                         const float u, const float2 u2)
        {
            BSDFSample ret;
            ret.flags = BSDFSample::Flags::Glossy | BSDFSample::Flags::Reflection; 

            let a = 0.25;
            let a2 = a * a;

            let c = sqrt(max(0.0, (1.0 - pow(a2, 1.0 - u2.x)) / (1.0 - a2)));
            let s = sqrt(max(0.0, 1.0 - c * c));
            let phi = 2. * k::pi * u2.y;

            float3 wm = float3(s * cos(phi), c, s * sin(phi));
            if(dot(wm, wi) < 0.0) 
            {
                wm = -wm;
            }

            ret.wo = reflect(-wi, wm);
            if(dot(wi, ret.wo) < 0.0)
            {
                return none;
            }

            let clearcoatWeight = params.clearcoat;
            let clearcoatGloss = params.clearcoatGloss;

            let dotNH    = pbr::cosTheta(wm);
            let dotLH    = dot(wm, ret.wo);
            let absDotNL = abs(pbr::cosTheta(ret.wo));
            let absDotNV = abs(pbr::cosTheta(wi));

            let d = pbr::D_GTR1(abs(dotNH), lerp(0.1, 0.001, clearcoatGloss));
            let f = pbr::schlick(0.04, dotLH);
            let g = pbr::separableSmithGGXG1(wi, 0.25) * pbr::separableSmithGGXG1(ret.wo, 0.25);

            let fPdf = d / (4.0 * dot(wi, wm));
            // ?
            ret.f = float3(0.25 * clearcoatWeight * g * f * d) / fPdf;
            ret.pdf = fPdf;

            return ret;
        }

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
            let thin = true; // HACK: 

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
                if (let res = sampleDisneyClearcoat(params, si.wi, sample1, sample2))
                {
                    var ret = res;
                    ret.pdf *= pClearcoat;
                    return ret;                
                }
            }
            else if(sample1 > pSpecular + pClearcoat && sample1 <= (pSpecular + pClearcoat + pDiffuse)) 
            {
                if (let res = Lambert.BSDF.sample(params, ctx, si, sample1, sample2))
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
            let thin = true; // HACK:
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

            // while pdf
            float pdf = 0.0;

            // clearcoat
            let upperHemisphere = dotNL > 0.0 && dotNV > 0.0;
            if (upperHemisphere && params.clearcoat > 0.0)
            {
                let absDotNL = abs(pbr::cosTheta(wo));
                let absDotNH = abs(pbr::cosTheta(wm));
                let d        = pbr::D_GTR1(absDotNH, lerp(0.1, 0.001, params.roughness * params.roughness));
                pdf += pClearcoat * d / (4.0 * absDotNL);
            }

            // diffuse
            if (diffuseWeight > 0.0)
            {
                pdf += pDiffuse * Lambert.BSDF.pdf(params, ctx, si, wo);
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

            return pdf;
        }

        public static float3 eval(MaterialParams params, BSDFContext ctx, SurfaceInteraction si, float3 wo)
        {
            let thin = true; // HACK:
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
                let clearcoat = evaluateDisneyClearcoat(params.clearcoat, params.clearcoatGloss, si.wi, wm, wo);
                reflectance += float3(clearcoat);
            }

            // diffuse
            if(diffuseWeight > 0.0) 
            {
                let diffuse = Lambert.BSDF.eval(params, ctx, si, wo);

                float3 sheen = evaluateSheen(params.albedo, params.sheen, params.sheenTint, si.wi, wm, wo);

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