module PBR;

import "../Utility/Constants";

namespace pbr
{
    [ForceInline]
    public float sgn(const float v)
    {
        if (v == 0.)
        {
            return 0.;
        }

        return v >= 0. ? 1. : -1.;
    }

    [ForceInline]
    public float sqr(const float x)
    {
        return x * x;
    }

    [ForceInline]
    public float cosTheta(const float3 v)
    {
        return v.z; // Z-UP shading space
    }

    [ForceInline]
    public float cos2Theta(const float3 v)
    {
        let c = cosTheta(v);
        return c * c;
    }

    [ForceInline]
    public float sinTheta(const float3 v)
    {
        return sqrt(1. - cos2Theta(v));
    }

    [ForceInline]
    public float tanTheta(const float3 v)
    {
        let c = cosTheta(v);
        return sqrt(1. - c * c) / c;
    }

    [ForceInline]
    public float tan2Theta(const float3 v)
    {
        let c2 = cos2Theta(v);
        return 1. / c2 - 1.;
    }

    [ForceInline]
    public float cosPhi(const float3 v)
    {
        let s = sinTheta(v);
        return (s == 0) ? 1. : clamp(v.x / s, -1., 1.);
    }

    [ForceInline]
    public float cos2Phi(const float3 v)
    {
        let c = cosPhi(v);
        return c * c;
    }

    [ForceInline]
    public float sin2Phi(const float3 v)
    {
        let c2 = cos2Phi(v);
        return 1 - c2;
    }

    [ForceInline]
    public float sinPhi(const float3 v)
    {
        return sqrt(sin2Phi(v));
    }

    [ForceInline]
    public float schlick(const float cosine, const float ref_idx)
    {
        float r0 = (1. - ref_idx) / (1. + ref_idx);
        r0 = r0 * r0;
        return r0 + (1. - r0) * pow((1. - cosine), 5.);
    }

    [ForceInline]
    public float3 schlickfloat3(const float3 r0, const float radians)
    {
        let exponential = pow(1. - radians, 5.);
        return r0 + (float3(1.) - r0) * exponential;
    }

    [ForceInline]
    public float schlickWeight(const float u)
    {
        let m = clamp(1. - u, 0., 1.);
        let m2 = m * m;
        return m * m2 * m2;
    }

    [ForceInline]
    public bool isEffectivelySmooth(const float2 alpha)
    {
        return max(alpha.x, alpha.y) < k::eps;
    }

    [ForceInline]
    public bool isEffectivelySmooth(const float ax, const float ay)
    {
        return max(ax, ay) < k::eps;
    }

    public bool refract(const float3 wi, float3 normal, float eta, out float etap, out float3 wt)
    {
        float ct_i = dot(normal, wi);
        if (ct_i < 0.)
        {
            eta = 1. / eta;
            ct_i = -ct_i;
            normal = -normal;
        }

        let s2t_i = max(0., 1. - sqr(ct_i));
        let s2t_t = s2t_i / sqr(eta);
        if (s2t_t >= 1.)
        {
            etap = 0.0;
            wt = k::zeros.xyz;
            return false;
        }

        let ct_t = sqrt(1. - s2t_t);
        wt = -wi / eta + (ct_i / eta - ct_t) * normal;
        etap = eta;

        return true;
    }

    public float fresnelDielectric(float cosThetaI, float etaI, float etaT)
    {
        cosThetaI = clamp(cosThetaI, -1., 1.);
        // potentially swap indices of refraction
        let entering = cosThetaI >= 0.;
        if (!entering)
        {
            let tmp = etaI;
            etaI = etaT;
            etaT = tmp;

            cosThetaI = abs(cosThetaI);
        }

        // compute cosThetaT using Snell's law
        let sinThetaI = sqrt(max(0., 1. - cosThetaI * cosThetaI));
        let sinThetaT = etaI / etaT * sinThetaI;

        // handle total internal reflection
        if (sinThetaT >= 1.) 
        {
            return 1.;
        }

        let cosThetaT = sqrt(max(0., 1. - sinThetaT * sinThetaT));
        let Rparl = ((etaT * cosThetaI) - (etaI * cosThetaT)) /
                    ((etaT * cosThetaI) + (etaI * cosThetaT));
        let Rperp = ((etaI * cosThetaI) - (etaT * cosThetaT)) /
                    ((etaI * cosThetaI) + (etaT * cosThetaT));
        return (Rparl * Rparl + Rperp * Rperp) / 2.;
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
        // Disney scales by (.65 * eta - .35) based on figure 15 of the 2015 PBR course notes. Based on their figure
        // the results match a geometrically thin solid fairly well
        return clamp((0.65 * IOR - 0.35) * roughness, 0.0, 1.0);
    }

    public float D_GTR1(const float absDotHL, const float a)
    {
        if(a >= 1.0) 
        {
            return k::invPi;
        }

        let a2 = a * a;
        return (a2 - 1.0) / (k::pi * log2(a2) * (1.0 + (a2 - 1.0) * absDotHL * absDotHL));
    }

    public float separableSmithGGXG1(const float3 w, const float a)
    {
        let a2 = a * a;
        let absDotNV = abs(cosTheta(w));

        return 2.0 / (1.0 + sqrt(a2 + (1.0 - a2) * absDotNV * absDotNV));
    }

    public float GGXAnisotropicD(const float3 wm, const float ax, const float ay)
    {
        let t = tan2Theta(wm) * (cos2Phi(wm) / ax / ax + sin2Phi(wm) / ay / ay);
        let c2 = cos2Theta(wm);
        let denom = k::pi * ax * ay * c2 * c2 * (1 + t) * (1 + t);

        return 1. / denom;
    }

    public float separableSmithGGXG1(const float3 w, const float3 wm, const float ax, const float ay)
    {
        let absTanTheta = abs(tanTheta(w));
        if(absTanTheta >= k::infty) 
        {
            return 0.0;
        }

        let a             = sqrt(cos2Phi(w) * ax * ax + sin2Phi(w) * ay * ay);
        let a2Tan2Theta = (a * absTanTheta) * (a * absTanTheta);

        let lambda = 0.5 * (-1.0 + sqrt(1.0 + a2Tan2Theta));
        return 1.0 / (1.0 + lambda);
    }

    public float GGXVNDFAnisotropicPdf(const float3 wo, const float3 wm, const float3 wi, const float ax, const float ay)
    {
        let absDotNL = abs(cosTheta(wo));
        let absDotLH = abs(dot(wm, wo));
        let G1 = separableSmithGGXG1(wi, wm, ax, ay);
        let D = GGXAnisotropicD(wm, ax, ay);

        return G1 * absDotLH * D / absDotNL;
    }

    public void calcAnisotropic(const float roughness, const float anisotropic, out float ax, out float ay)
    {
        let aspect  = sqrt(1.0 - 0.9 * anisotropic);
        let r2      = roughness * roughness;

        ax          = r2 / aspect;
        ay          = r2 * aspect;
    }

    public float fresnelSchlickR0FromRelativeIOR(const float relativeIOR)
    {
        let r0 = (1 - relativeIOR) / (1 + relativeIOR);
        return r0 * r0;
    }

    public float3 calculateTint(const float3 baseColor)
    {
        let luminance = dot(float3(0.3, 0.6, 1.0), baseColor);
        return (luminance > 0.0) ? baseColor * (1.0 / luminance) : float3(1.0);
    }

    public float3 disneyFresnel(const float3 baseColor, const float specularTint, const float metallic, const float IOR, const float relativeIOR, const float3 wi, const float3 wm, const float3 wo)
    {
        let dotHV = abs(dot(wm, wi));

        let tint = calculateTint(baseColor);

        float3 R0 = fresnelSchlickR0FromRelativeIOR(relativeIOR) * lerp(float3(1.0), tint, specularTint);
        R0 = lerp(R0, baseColor, metallic);

        let dielectricFresnel = fresnelDielectric(dotHV, 1.0, IOR);
        let metallicFresnel = schlickfloat3(R0, dot(wo, wm));

        return lerp(float3(dielectricFresnel), metallicFresnel, metallic);
    }
}