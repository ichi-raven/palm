implementing Material;

import "../Utility/Constants";
import "../Utility/Warp";
import PBR;

public struct Principle : IMaterial
{
    public __init(in MaterialParams params_)
    {
        //mParams = params_;
    }

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

            const float dotHL = dot(wm, wo);
            const float3 tint = pbr::calculateTint(baseColor);
            return sheen * lerp(float3(1.0), tint, sheenTint) * pbr::schlickWeight(dotHL);
        }

        static float3 evaluateDisneyBRDF(const MaterialParams params, const BSDFContext ctx, const float3 wi, const float3 wm,
                                        const float3 wo)
        {
            let relativeIOR = ctx.lastIOR / params.IOR;

            float dotNL = pbr::cosTheta(wo);
            float dotNV = pbr::cosTheta(wi);
            if(dotNL <= 0.0 || dotNV <= 0.0) 
            {
                return float3(0.0);
            }

            float2 a;
            pbr::calcAnisotropic(params.roughness, params.anisotropic, a.x, a.y);

            let d = pbr::GGXAnisotropicD(wm, a.x, a.y);
            let gl = pbr::separableSmithGGXG1(wi, wm, a.x, a.y);
            let gv = pbr::separableSmithGGXG1(wo, wm, a.x, a.y);

            float3 f = pbr::disneyFresnel(params.albedo, params.specularTint, params.metallic, params.IOR, relativeIOR, wo, wm, wi);

            return d * gl * gv * f / (4.0 * dotNL * dotNV);
        }

        static float3 evaluateDisneySpecTransmission(const MaterialParams params, const BSDFContext ctx, const float3 wi, const float3 wm,
                                                    const float3 wo, const float ax, const float ay, const bool thin)
        {
            let relativeIOR = ctx.lastIOR / params.IOR;
            let n2 = relativeIOR * relativeIOR;

            let absDotNL = abs(pbr::cosTheta(wo));
            let absDotNV = abs(pbr::cosTheta(wi));
            let dotHL = dot(wm, wo);
            let dotHV = dot(wm, wi);
            let absDotHL = abs(dotHL);
            let absDotHV = abs(dotHV);

            let d = pbr::GGXAnisotropicD(wm, ax, ay);
            let gl = pbr::separableSmithGGXG1(wi, wm, ax, ay);
            let gv = pbr::separableSmithGGXG1(wo, wm, ax, ay);

            float f = pbr::fresnelDielectric(dotHV, 1.0, 1.0 / relativeIOR);

            float3 color;
            if(thin)
            {
                color = sqrt(params.albedo);
            }
            else
            {
                color = params.albedo;
            }
            
            // Note that we are intentionally leaving out the 1/n2 spreading factor since for VCM we will be evaluating
            // particles with this. That means we'll need to model the air-[other medium] transmission if we ever place
            // the camera inside a non-air medium.
            let c = (absDotHL * absDotHV) / max(k::eps, absDotNL * absDotNV);
            let tmp = dotHL + relativeIOR * dotHV;
            let t = (n2 / max(k::eps, tmp * tmp));
            return color * c * t * (1.0 - f) * gl * gv * d;
        }

        static float evaluateDisneyRetroDiffuse(float roughness, const float3 wi, const float3 wm, const float3 wo)
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
                                        const float3 wo, bool thin)
        {
            let dotNL = abs(pbr::cosTheta(wo));
            let dotNV = abs(pbr::cosTheta(wi));

            let fl = pow((1.0 - dotNL), 5.);
            let fv = pow((1.0 - dotNV), 5.);

            float hanrahanKrueger = 0.0;

            if(thin && flatness > 0.0) 
            {
                float alpha = roughness * roughness;

                float dotHL = dot(wm, wo);
                float fss90 = dotHL * dotHL * alpha;
                float fss = lerp(1.0, fss90, fl) * lerp(1.0, fss90, fv);

                hanrahanKrueger = 1.25 * (fss * (1.0 / (dotNL + dotNV) - 0.5) + 0.5);
            }

            float lambert = 1.0;
            // what
            float retro = evaluateDisneyRetroDiffuse(roughness, wi, wm, wo);
            float subsurfaceApprox = lerp(lambert, hanrahanKrueger, thin ? flatness : 0.0);

            return k::invPi * (retro + subsurfaceApprox * (1.0 - 0.5 * fl) * (1.0 - 0.5 * fv));
        }

        static float3 sampleGGXVNDFAnisotropic(const float3 wi, float ax, float ay, float2 u2)
        {
            // -- Stretch the view vector so we are sampling as though roughness==1
            let v = normalize(float3(wi.x * ax, wi.y * ay, wi.z));
            // -- Build an orthonormal basis with v, t1, and t2
            let t1 = (v.z < 1. - k::eps) ? normalize(cross(v, float3(0., 0., 1.))) : float3(1., 0., 0.);
            let t2 = cross(t1, v);
            // -- Choose a point on a disk with each half of the disk weighted proportionally to its projection onto direction v
            let a = 1. / (1. + v.z);
            let r = sqrt(u2.x);
            let phi = (u2.y < a) ? (u2.y / a) * k::pi : k::pi + (u2.y - a) / (1. - a) * k::pi;
            let p1 = r * cos(phi);
            let p2 = r * sin(phi) * ((u2.y < a) ? 1. : v.z);
            // -- Calculate the normal in this stretched tangent space
            float3 n = p1 * t1 + p2 * t2 + sqrt(max(0., 1. - p1 * p1 - p2 * p2)) * v;
            // -- unstretch and normalize the normal
            return normalize(float3(ax * n.x, ay * n.y, n.z));
        }

        static Optional<BSDFSample> sampleDisneySpecTransmission(const MaterialParams params, const BSDFContext ctx, const float3 wi, const bool thin, const float u, const float2 u2)
        {
            
            if(pbr::cosTheta(wi) == 0.0) 
            {
                return none;
            }
                
            BSDFSample ret;

            // -- Scale roughness based on IOR
            float rscaled = thin ? pbr::thinTransmissionRoughness(params.IOR, params.roughness) : params.roughness;
            
            float tax, tay;
            pbr::calcAnisotropic(rscaled, params.anisotropic, tax, tay);
            
            // -- Sample visible distribution of normals
            let wm = sampleGGXVNDFAnisotropic(wi, tax, tay, u2);

            float dotVH = dot(wi, wm);
            if(wm.y < 0.0) 
            {
                dotVH = -dotVH;
            }

            let ni = wi.y > 0.0 ? 1.0 : params.IOR;
            let nt = wi.y > 0.0 ? params.IOR : 1.0;
            let relativeIOR = ni / nt;

            // -- Disney uses the full dielectric Fresnel equation for transmission. We also importance sample F
            // -- to switch between refraction and reflection at glancing angles.
            let F = pbr::fresnelDielectric(dotVH, 1.0, params.IOR);
            
            // -- Since we're sampling the distribution of visible normals the pdf cancels out with a number of other terms.
            // -- We are left with the weight G2(wi, wo, wm) / G1(wi, wm) and since Disney uses a separable masking function
            // -- we get G1(wi, wm) * G1(wo, wm) / G1(wi, wm) = G1(wo, wm) as our weight.
            let G1v = pbr::separableSmithGGXG1(wi, wm, tax, tay);

            float pdf;
            if(u <= F) 
            {
                ret.wo = normalize(reflect(-wi, wm));

                ret.flags = BSDFSample::Flags::Diffuse | BSDFSample::Flags::Transmission;
                ret.f = G1v * params.albedo;

                let jacobian = (4. * abs(dot(wi, wm)));
                pdf = F / jacobian;
            }
            else 
            {
                if(thin) 
                {
                    // -- When the surface is thin so it refracts into and then out of the surface during this shading event.
                    // -- So the ray is just reflected then flipped and we use the sqrt of the surface color.
                    ret.wo = reflect(-wi, wm);
                    ret.wo.y = -ret.wo.y;
                    ret.f = G1v * sqrt(params.albedo);

                    // -- Since this is a thin surface we are not ending up inside of a volume so we treat this as a scatter event.
                    ret.flags = BSDFSample::Flags::Diffuse | BSDFSample::Flags::Reflection;
                }
                else 
                {
                    if(pbr::transmit(wm, wi, relativeIOR, ret.wo)) 
                    {
                        ret.flags = BSDFSample::Flags::Specular | BSDFSample::Flags::Transmission;
                        // TODO: currently ignore medium
                        //sample_out.medium.phaseFunction = dotVH > 0.0 ? MediumPhaseFunction::eIsotropic : MediumPhaseFunction::eVacuum;
                        //sample_out.medium.extinction = CalculateExtinction(surface.transmittanceColor, surface.scatterDistance);
                    }
                    else 
                    {
                        ret.flags = BSDFSample::Flags::Diffuse | BSDFSample::Flags::Reflection;;
                        ret.wo = reflect(-wi, wm);
                    }

                    ret.f = G1v * params.albedo;
                }

                ret.wo = normalize(ret.wo);
                
                float dotLH = abs(dot(ret.wo, wm));
                let relativeIOR = ctx.lastIOR / params.IOR;
                float tmp = dotLH + relativeIOR * dotVH;
                float jacobian = dotLH / (tmp * tmp);
                pdf = (1.0 - F) / jacobian;
            }

            if(pbr::cosTheta(ret.wo) == 0.0)
            {
                return none;
            }

            if(params.roughness < 0.01) 
            {
                // -- This is a hack to allow us to sample the correct IBL texture when a path bounced off a smooth surface.
                ret.flags = ret.flags | BSDFSample::Flags::Specular | BSDFSample::Flags::Reflection;
            }

            // -- calculate VNDF pdf terms and apply Jacobian and Fresnel sampling adjustments
            ret.pdf *= pbr::GGXVNDFAnisotropicPdf(ret.wo, wm, wi, tax, tay);

            return ret;
        }

        static Optional<BSDFSample> sampleDisneyDiffuse(const MaterialParams params, float3 wi, bool thin,
                                        const float u, const float2 u2)
        {
            BSDFSample ret;

            float sign = pbr::sgn(pbr::cosTheta(wi));

            ret.wo = sign * Warp::toCosineWeightedHemisphere(u2);
            float3 wm = normalize(wi + ret.wo);

            let dotNL = pbr::cosTheta(ret.wo);
            if(dotNL == 0.0) 
            {
                return none;
            }

            float dotNV = pbr::cosTheta(wi);
            float3 color = params.albedo;
            float pdf;
            if(u <= params.diffTrans) 
            {
                wi = -wi;
                pdf = params.diffTrans;

                color = sqrt(color);
            }
            else 
            {
                pdf = (1.0 - params.diffTrans);
            }

            let sheen = evaluateSheen(params.albedo, params.sheen, params.sheenTint, wi, wm, ret.wo);

            float diffuse = evaluateDisneyDiffuse(params.albedo, params.flatness, params.roughness, wi, wm, ret.wo, thin);

            ret.f = sheen + color * diffuse;
            ret.pdf = abs(dotNL) * pdf;
            ret.flags = BSDFSample::Flags::Diffuse | BSDFSample::Flags::Reflection;

            return ret;
        }

        static Optional<BSDFSample> sampleDisneyClearcoat(const MaterialParams params, const float3 wi,
                                         const float u, const float2 u2)
        {
            BSDFSample ret;

            let a = 0.25;
            let a2 = a * a;

            float c = sqrt(max(0.0, (1.0 - pow(a2, 1.0 - u2.x)) / (1.0 - a2)));
            float s = sqrt(max(0.0, 1.0 - c * c));
            float phi = 2. * k::pi * u2.y;

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

            float clearcoatWeight = params.clearcoat;
            float clearcoatGloss = params.clearcoatGloss;

            let dotNH    = pbr::cosTheta(wm);
            let dotLH    = dot(wm, ret.wo);
            let absDotNL = abs(pbr::cosTheta(ret.wo));
            let absDotNV = abs(pbr::cosTheta(wi));

            let d = pbr::D_GTR1(abs(dotNH), lerp(0.1, 0.001, clearcoatGloss));
            let f = pbr::schlick(0.04, dotLH);
            let g = pbr::separableSmithGGXG1(wi, 0.25) * pbr::separableSmithGGXG1(ret.wo, 0.25);

            float fPdf = d / (4.0 * dot(wi, wm));

            ret.f = float3(0.25 * clearcoatWeight * g * f * d) / fPdf;
            // tangent space
            ret.pdf = fPdf;

            return ret;
        }

        static Optional<BSDFSample> sampleDisneyBRDF(const MaterialParams params, const BSDFContext ctx, const float3 wi,
                                        const float u, const float2 u2)
        {
            BSDFSample ret;
            let relativeIOR = ctx.lastIOR / params.IOR;

            // -- Calculate Anisotropic params
            float ax, ay;
            pbr::calcAnisotropic(params.roughness, params.anisotropic, ax, ay);

            // -- Sample visible distribution of normals
            let wm = sampleGGXVNDFAnisotropic(wi, ax, ay, u2);

            // -- Reflect over wm
            ret.wo = normalize(reflect(-wi, wm));
            if(ret.wo.z < 0.)
            {
                return none;
            }

            // -- Fresnel term for this lobe is complicated since we're blending with both the metallic and the specularTint
            // -- parameters plus we must take the IOR into account for dielectrics
            let F = pbr::disneyFresnel(params.albedo, params.specularTint, params.metallic, params.IOR, relativeIOR, wi, wm, ret.wo);

            // -- Since we're sampling the distribution of visible normals the pdf cancels out with a number of other terms.
            // -- We are left with the weight G2(wi, wo, wm) / G1(wi, wm) and since Disney uses a separable masking function
            // -- we get G1(wi, wm) * G1(wo, wm) / G1(wi, wm) = G1(wo, wm) as our weight.
            let G1v = pbr::separableSmithGGXG1(wi, wm, ax, ay);
            let specular = G1v * F;

            ret.flags = BSDFSample::Flags::Diffuse | BSDFSample::Flags::Reflection;
            ret.f = specular;
            ret.pdf = pbr::GGXVNDFAnisotropicPdf(ret.wo, wm, wi, ax, ay);

            //ret.pdf *= (1.0 / (4. * abs(dot(ret.wo, wm))));

            return ret;
        }

        public static void calcLobePdfs(const MaterialParams params,
                              out float pSpecular, out float pDiffuse, out float pClearcoat, out float pSpecTrans)
        {
            // DEBUG:
            // pSpecular = 1.0;
            // pDiffuse = 0.0;
            // pClearcoat = 0.0;
            // pSpecTrans = 0.0;
            // return;

            let metallicBRDF   = params.metallic;
            let specularBSDF   = (1.0 - params.metallic) * params.specTrans;
            let dielectricBRDF = (1.0 - params.specTrans) * (1.0 - params.metallic);

            let specularWeight     = metallicBRDF + dielectricBRDF;
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
            
            if(sample1 <= pSpecular) 
            {
                if (let res = sampleDisneyBRDF(params, ctx, si.wi, sample1, sample2))
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
                if (let res = sampleDisneyDiffuse(params, si.wi, thin, sample1, sample2))
                {
                    var ret = res;
                    ret.pdf *= pDiffuse;
                    return ret; 
                }
            }
            else if(pTransmission >= 0.0) 
            {
                if (let res = sampleDisneySpecTransmission(params, ctx, si.wi, false, sample1, sample2))
                {
                    var ret = res;
                    ret.pdf *= pTransmission;
                    return ret; 
                }
            }

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

            // pdf
            float wholePDF = 0.0;

            // clearcoat
            let upperHemisphere = dotNL > 0.0 && dotNV > 0.0;
            if(upperHemisphere && params.clearcoat > 0.0)
            {
                let absDotNL = abs(pbr::cosTheta(wo));
                let absDotNH = abs(pbr::cosTheta(wm));
                let d = pbr::D_GTR1(absDotNH, lerp(0.1, 0.001, params.roughness * params.roughness));
                wholePDF += pClearcoat * d / (4.0 * absDotNL);
            }

            // diffuse
            if(diffuseWeight > 0.0) 
            {
                wholePDF += pDiffuse * abs(pbr::cosTheta(wo));
            }

            // transmission
            if(transWeight > 0.0) 
            {
                // Scale roughness based on IOR (Burley 2015, Figure 15).
                let rscaled = thin ? pbr::thinTransmissionRoughness(params.IOR, params.roughness) : params.roughness;
                float tax, tay;
                pbr::calcAnisotropic(rscaled, params.anisotropic, tax, tay);

                let pdf = pbr::GGXVNDFAnisotropicPdf(wo, wm, si.wi, tax, tay);

                let dotLH = dot(wm, wo);
                let dotVH = dot(wm, si.wi);
                let relativeIOR = ctx.lastIOR / params.IOR;
                let v = dotVH + relativeIOR * dotLH;
                wholePDF += pSpecTrans * pdf / (v * v);
            }

            // specular
            if(upperHemisphere) 
            {                
                let pdf = pbr::GGXVNDFAnisotropicPdf(wo, wm, si.wi, ax, ay) / (4.0 * abs(dot(wo, wm)));
                wholePDF += pBRDF * pdf / (4. * abs(dot(si.wi, wm)));
            }

            return clamp(wholePDF, 0.0, 1.0);
        }

        public static float3 eval(MaterialParams params, BSDFContext ctx, SurfaceInteraction si, float3 wo)
        {
            let thin = true; // HACK:
            let wm = normalize(si.wi + wo);

            let dotNV = pbr::cosTheta(si.wi);
            let dotNL = pbr::cosTheta(wo);

            float3 reflectance = float3(0.0);

            float pBRDF = 0.0, pDiffuse = 0.0, pClearcoat = 0.0, pSpecTrans = 0.0;
            calcLobePdfs(params, pBRDF, pDiffuse, pClearcoat, pSpecTrans);

            // calculate all of the anisotropic params
            float ax, ay;
            pbr::calcAnisotropic(params.roughness, params.anisotropic, ax, ay);

            float diffuseWeight = (1.0 - params.metallic) * (1.0 - params.specTrans);
            float transWeight   = (1.0 - params.metallic) * params.specTrans;

            // -- Clearcoat
            bool upperHemisphere = dotNL > 0.0 && dotNV > 0.0;
            if(upperHemisphere && params.clearcoat > 0.0)
            {
                let clearcoat = evaluateDisneyClearcoat(params.clearcoat, params.clearcoatGloss, si.wi, wm, wo);
                reflectance += float3(clearcoat);
            }

            // -- Diffuse
            if(diffuseWeight > 0.0) 
            {
                let diffuse = evaluateDisneyDiffuse(params.albedo, params.flatness, params.roughness, si.wi, wm, wo, thin);

                float3 sheen = evaluateSheen(params.albedo, params.sheen, params.sheenTint, si.wi, wm, wo);

                reflectance += diffuseWeight * (diffuse * params.albedo + sheen);
            }

            // -- transmission
            if(transWeight > 0.0) 
            {
                // Scale roughness based on IOR (Burley 2015, Figure 15).
                float rscaled = thin ? pbr::thinTransmissionRoughness(params.IOR, params.roughness) : params.roughness;
                float tax, tay;
                pbr::calcAnisotropic(rscaled, params.anisotropic, tax, tay);

                float3 transmission = evaluateDisneySpecTransmission(params, ctx, si.wi, wm, wo, tax, tay, thin);
                reflectance += transWeight * transmission;
            }

            // -- specular
            if(upperHemisphere) 
            {
                let specular = evaluateDisneyBRDF(params, ctx, si.wi, wm, wo);
                
                reflectance += specular;
            }

            reflectance *= abs(dotNL);

            return reflectance;
        }
    }

}