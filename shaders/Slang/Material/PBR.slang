module PBR;

import "../Utility/Constants";

namespace pbr
{
    public float sgn(const float v)
    {
        if (v == 0.0)
        {
            return 0.0;
        }

        return v >= 0. ? 1.0 : -1.0;
    }

    public float cosTheta(const float3 v)
    {
        return v.z; // Z-UP shading space
    }

    public float cos2Theta(const float3 v)
    {
        const float c = cosTheta(v);
        return c * c;
    }

    public float sinTheta(const float3 v)
    {
        return sqrt(1.0 - cos2Theta(v));
    }

    public float tanTheta(const float3 v)
    {
        float c = cosTheta(v);
        return sqrt(1.0 - c * c) / c;
    }

    public float cosPhi(const float3 v)
    {
        float s = sinTheta(v);
        return (s == 0) ? 1.0 : clamp(v.x / s, -1.0, 1.0);
        //return dot(normalize(v), float3(1., 0., 0.));
    }

    public float cos2Phi(const float3 v)
    {
        const float c = cosPhi(v);
        return c * c;
    }

    public float sin2Phi(const float3 v)
    {
        const float c2 = cos2Phi(v);
        return 1 - c2;
    }

    public float sinPhi(const float3 v)
    {
        return sqrt(sin2Phi(v));
    }

    public float schlick(const float cosine, const float ref_idx)
    {
        float r0 = (1. - ref_idx) / (1. + ref_idx);
        r0 = r0 * r0;
        return r0 + (1. - r0) * pow((1 - cosine), 5.);
    }

    public float3 schlickfloat3(const float3 r0, const float radians)
    {
        let exponential = pow(1.0 - radians, 5.0);
        return r0 + (float3(1.0) - r0) * exponential;
    }

    public float schlickWeight(const float u)
    {
        const float m = clamp(1.0 - u, 0.0, 1.0);
        const float m2 = m * m;
        return m * m2 * m2;
    }

    public float fresnelDielectric(float cosThetaI, float etaI, float etaT)
    {
        cosThetaI = clamp(cosThetaI, -1.0, 1.0);
        // Potentially swap indices of refraction
        const bool entering = cosThetaI > 0.0;
        if (!entering) 
        {
            float tmp = etaI;
            etaI = etaT;
            etaT = tmp;

            cosThetaI = abs(cosThetaI);
        }

        // Compute _cosThetaT_ using Snell's law
        const float sinThetaI = sqrt(max(0.0, 1.0 - cosThetaI * cosThetaI));
        const float sinThetaT = etaI / etaT * sinThetaI;

        // Handle total internal reflection
        if (sinThetaT >= 1.0) 
        {
            return 1.0;
        }

        const float cosThetaT = sqrt(max(0.0, 1.0 - sinThetaT * sinThetaT));
        const float Rparl = ((etaT * cosThetaI) - (etaI * cosThetaT)) /
                    ((etaT * cosThetaI) + (etaI * cosThetaT));
        const float Rperp = ((etaI * cosThetaI) - (etaT * cosThetaT)) /
                    ((etaI * cosThetaI) + (etaT * cosThetaT));
        return (Rparl * Rparl + Rperp * Rperp) / 2.0;
    }

    public bool transmit(float3 wm, float3 wi, float n, out float3 wo)
    {
        float c = dot(wi, wm);
        if(c < 0.0) 
        {
            c = -c;
            wm = -wm;
        }
        float root = 1.0f - n * n * (1.0 - c * c);
        if(root <= 0)
        {
            wo = k::zeros.xyz;
            return false;
        }

        wo = (n * c - sqrt(root)) * wm - n * wi;
        return true;
    }

    public float thinTransmissionRoughness(const float IOR, const float roughness)
    {
        // -- Disney scales by (.65 * eta - .35) based on figure 15 of the 2015 PBR course notes. Based on their figure
        // -- the results match a geometrically thin solid fairly well.
        return clamp((0.65 * IOR - 0.35) * roughness, 0.0, 1.0);
    }

    public float D_GTR1(const float absDotHL, const float a)
    {
        if(a >= 1.0) 
        {
            return k::invPi;
        }

        const float a2 = a * a;
        return (a2 - 1.0) / (k::pi * log2(a2) * (1.0 + (a2 - 1.0) * absDotHL * absDotHL));
    }

    public float separableSmithGGXG1(const float3 w, const float a)
    {
        const float a2 = a * a;
        const float absDotNV = abs(w.z);

        return 2.0 / (1.0 + sqrt(a2 + (1 - a2) * absDotNV * absDotNV));
    }

    public float GGXAnisotropicD(const float3 wm, const float ax, const float ay)
    {
        let dotHX2 = wm.x * wm.x;
        let dotHY2 = wm.z * wm.z;
        let ct = wm.z;
        let cos2Theta = ct * ct;
        let ax2 = ax * ax;
        let ay2 = ay * ay;

        let tmp = dotHX2 / ax2 + dotHY2 / ay2 + cos2Theta;
        return 1.0 / (k::pi * ax * ay * tmp * tmp);
    }

    public float separableSmithGGXG1(const float3 w, const float3 wm, const float ax, const float ay)
    {
        let dotHW = dot(w, wm);
        if (dotHW <= 0.0) 
        {
            return 0.0;
        }

        const float absTanTheta = abs(tanTheta(w));
        if(absTanTheta >= k::infty) 
        {
            return 0.0;
        }

        let a = sqrt(cos2Phi(w) * ax * ax + sin2Phi(w) * ay * ay);
        float a2Tan2Theta = (a * absTanTheta);
        a2Tan2Theta *= a2Tan2Theta;

        let lambda = 0.5 * (-1.0 + sqrt(1.0 + a2Tan2Theta));
        return 1.0 / max(k::eps, 1.0 + lambda);
    }

    public float GGXVNDFAnisotropicPdf(const float3 wo, const float3 wm, const float3 wi, const float ax, const float ay)
    {
        let absDotNL = abs(cosTheta(wo));
        let absDotLH = abs(dot(wm, wo));
        let G1 = separableSmithGGXG1(wi, wm, ax, ay);
        let D = GGXAnisotropicD(wm, ax, ay);

        return G1 * absDotLH * D / absDotNL;
    }

    // public void GGXVNDFAnisotropicPdfOut2(const float3 wi, const float3 wm, const float3 wo, float ax, float ay,
    //                                 out float forwardPdfW, out float reversePdfW)
    // {
    //     float D = GGXAnisotropicD(wm, ax, ay);
    //     float absDotNL = abs(cosTheta(wi));
    //     float absDotHL = abs(dot(wm, wi));
    //     float G1v = separableSmithGGXG1(wo, wm, ax, ay);
    //     forwardPdfW = G1v * absDotHL * D / absDotNL;
    //     float absDotNV = abs(cosTheta(wo));
    //     float absDotHV = abs(dot(wm, wo));
    //     float G1l = separableSmithGGXG1(wi, wm, ax, ay);
    //     reversePdfW = G1l * absDotHV * D / absDotNV;
    // }

    public void calcAnisotropic(const float roughness, const float anisotropic, out float ax, out float ay)
    {
        const float aspect = sqrt(1.0 - 0.9 * anisotropic);
        const float r2 = roughness * roughness;
        ax = r2 / aspect;
        ay = r2 * aspect;
        return;
    }

    public float fresnelSchlickR0FromRelativeIOR(const float relativeIOR)
    {
        const float r0 = (1 - relativeIOR) / (1 + relativeIOR);
        return r0 * r0;
    }

    public float3 calculateTint(const float3 baseColor)
    {
        // -- The color tint is never mentioned in the SIGGRAPH presentations as far as I recall but it was done in
        // --  the BRDF Explorer so I'll replicate that here.
        let luminance = dot(float3(0.3, 0.6, 1.0), baseColor);
        return (luminance > 0.0) ? baseColor * (1.0 / luminance) : float3(1.0);
    }

    public float3 disneyFresnel(const float3 baseColor, const float specularTint, const float metallic, const float IOR, const float relativeIOR, const float3 wi, const float3 wm, const float3 wo)
    {
        float dotHV = abs(dot(wm, wi));

        float3 tint = calculateTint(baseColor);

        // -- See section 3.1 and 3.2 of the 2015 PBR presentation + the Disney BRDF explorer (which does their
        // -- 2012 remapping rather than the SchlickR0FromRelativeIOR seen here but they mentioned the switch in 3.2).
        float3 R0 = fresnelSchlickR0FromRelativeIOR(relativeIOR) * lerp(float3(1.0), tint, specularTint);
        R0 = lerp(R0, baseColor, metallic);

        float dielectricFresnel = fresnelDielectric(dotHV, 1.0, IOR);
        float3 metallicFresnel = schlickfloat3(R0, dot(wo, wm));

        return lerp(float3(dielectricFresnel), metallicFresnel, metallic);
    }
}