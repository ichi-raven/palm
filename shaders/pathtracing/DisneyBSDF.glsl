#ifndef DISNEYBSDFS_GLSL_
#define DISNEYBSDFS_GLSL_

precision highp float;
precision highp int;

#extension GL_EXT_buffer_reference : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_shader_explicit_arithmetic_types : enable
#extension GL_EXT_control_flow_attributes : enable

#include "types.glsl"
#include "constants.glsl"
#include "randoms.glsl"

// Maths-------------------------------------
void basis(in vec3 n, out vec3 b1, out vec3 b2) 
{
    if(n.z<0.){
        float a = 1.0 / (1.0 - n.z);
        float b = n.x * n.y * a;
        b1 = vec3(1.0 - n.x * n.x * a, -b, n.x);
        b2 = vec3(b, n.y * n.y*a - 1.0, -n.y);
    }
    else{
        float a = 1.0 / (1.0 + n.z);
        float b = -n.x * n.y * a;
        b1 = vec3(1.0 - n.x * n.x * a, b, -n.x);
        b2 = vec3(b, 1.0 - n.y * n.y * a, -n.y);
    }
}

vec3 toWorld(vec3 x, vec3 y, vec3 z, vec3 v)
{
    return v.x*x + v.y*y + v.z*z;
}

vec3 toLocal(vec3 x, vec3 y, vec3 z, vec3 v)
{
    return vec3(dot(v, x), dot(v, y), dot(v, z));
}

float sgn(const float v)
{
    if (v == 0.0)
    {
        return 0.0;
    }

    return v >= 0. ? 1.0 : -1.0;
}

float cosTheta(const vec3 v)
{
    return v.y;
    //return dot(normalize(v), vec3(0., 1., 0.));
}

float cos2Theta(const vec3 v)
{
    const float c = cosTheta(v);
    return c * c;
}

float sinTheta(const vec3 v)
{
    return sqrt(1.0 - cos2Theta(v));
}

float tanTheta(const vec3 v)
{
    float c = cosTheta(v);
    return sqrt(1.0 - c * c) / c;
}

float cosPhi(const vec3 v)
{
    float s = sinTheta(v);
    return (s == 0) ? 1.0 : clamp(v.x / s, -1.0, 1.0);
    //return dot(normalize(v), vec3(1., 0., 0.));
}

float cos2Phi(const vec3 v)
{
    const float c = cosPhi(v);
    return c * c;
}

float sin2Phi(const vec3 v)
{
    const float c2 = cos2Phi(v);
    return 1 - c2;
}

float sinPhi(const vec3 v)
{
    return sqrt(sin2Phi(v));
}

float schlick(float cosine, float ref_idx);

vec3 schlickVec3(vec3 r0, float radians)
{
    float exponential = pow(1.0 - radians, 5.0);
    return r0 + (vec3(1.0) - r0) * exponential;
}

float schlickWeight(const float u)
{
    const float m = clamp(1.0 - u, 0.0, 1.0);
    const float m2 = m * m;
    return m * m2 * m2;
}

float FresnelDielectric(float cosThetaI, float etaI, float etaT)
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

bool Transmit(vec3 wm, vec3 wi, float n, out vec3 wo)
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
        return false;
    }

    wo = (n * c - sqrt(root)) * wm - n * wi;
    return true;
}

vec3 CalculateExtinction(vec3 apparantColor, float scatterDistance)
{
    vec3 a = apparantColor;
    vec3 a2 = a * a;
    vec3 a3 = a2 * a;

    vec3 alpha = vec3(1.0) - exp(-5.09406 * a + 2.61188 * a2 - 4.31805 * a3);
    vec3 s = vec3(1.9) - a + 3.5 * (a - vec3(0.8)) * (a - vec3(0.8));

    return 1.0 / (s * scatterDistance);
}

// Sheen-------------------------------------
vec3 calculateTint(const vec3 baseColor)
{
    // -- The color tint is never mentioned in the SIGGRAPH presentations as far as I recall but it was done in
    // --  the BRDF Explorer so I'll replicate that here.
    float luminance = dot(vec3(0.3, 0.6, 1.0), baseColor);
    return (luminance > 0.0) ? baseColor * (1.0 / luminance) : vec3(1.0);
}

vec3 EvaluateSheen(const vec3 baseColor, const float sheen, const vec3 sheenTint, const vec3 wo, const vec3 wm, const vec3 wi)
{
    if(sheen <= 0.0) 
    {
        return vec3(0.0);
    }

    const float dotHL = dot(wm, wi);
    const vec3 tint = calculateTint(baseColor);
    return sheen * mix(vec3(1.0), tint, sheenTint) * schlickWeight(dotHL);
}

// Clearcoat-------------------------------------
float D_GTR1(const float absDotHL, const float a)
{
    if(a >= 1.0) 
    {
        return M_INVPI;
    }

    const float a2 = a * a;
    return (a2 - 1.0) / (M_PI * log2(a2) * (1.0 + (a2 - 1.0) * absDotHL * absDotHL));
}

float separableSmithGGXG1(const vec3 w, const float a)
{
    const float a2 = a * a;
    const float absDotNV = abs(cosTheta(w));

    return 2.0 / (1.0 + sqrt(a2 + (1 - a2) * absDotNV * absDotNV));
}

float EvaluateDisneyClearcoat(const float clearcoat, const float alpha, const vec3 wo, const vec3 wm,
                                     const vec3 wi, out float fPdfW, out float rPdfW)
{
    if(clearcoat <= 0.0) 
    {
        return 0.0;
    }

    float absDotNH = abs(cosTheta(wm));
    float absDotNL = abs(cosTheta(wi));
    float absDotNV = abs(cosTheta(wo));
    float dotHL = dot(wm, wi);

    float d = D_GTR1(absDotNH, mix(0.1, 0.001, alpha));
    float f = schlick(0.04, dotHL);
    float gl = separableSmithGGXG1(wi, 0.25);
    float gv = separableSmithGGXG1(wo, 0.25);

    fPdfW = d / (4.0 * absDotNL);
    rPdfW = d / (4.0 * absDotNV);

    return 0.25 * clearcoat * d * f * gl * gv;
}

// Specular BRDF--------------------------------
float GGXAnisotropicD(const vec3 wm, const float ax, const float ay)
{
    const float dotHX2 = wm.x * wm.x;
    const float dotHY2 = wm.z * wm.z;
    const float ct = cosTheta(wm);
    const float cos2Theta = ct * ct;
    const float ax2 = ax * ax;
    const float ay2 = ay * ay;

    const float tmp = dotHX2 / ax2 + dotHY2 / ay2 + cos2Theta;
    return 1.0 / (M_PI * ax * ay * tmp * tmp);
}

float separableSmithGGXG1(const vec3 w, const vec3 wm, const float ax, const float ay)
{
    const float dotHW = dot(w, wm);
    if (dotHW <= 0.0) 
    {
        return 0.0;
    }

    const float absTanTheta = abs(tanTheta(w));
    if(absTanTheta >= INFTY) 
    {
        return 0.0;
    }

    float a = sqrt(cos2Phi(w) * ax * ax + sin2Phi(w) * ay * ay);
    float a2Tan2Theta = (a * absTanTheta);
    a2Tan2Theta *= a2Tan2Theta;

    float lambda = 0.5 * (-1.0 + sqrt(1.0 + a2Tan2Theta));
    return 1.0 / max(EPS, 1.0 + lambda);
}

float GgxVndfAnisotropicPdf(const vec3 wi, const vec3 wm, const vec3 wo, const float ax, const float ay)
{
    float absDotNL = abs(cosTheta(wi));
    float absDotLH = abs(dot(wm, wi));
    float G1 = separableSmithGGXG1(wo, wm, ax, ay);
    float D = GGXAnisotropicD(wm, ax, ay);

    return G1 * absDotLH * D / absDotNL;
}

void GgxVndfAnisotropicPdfOut2(const vec3 wi, const vec3 wm, const vec3 wo, float ax, float ay,
                                   out float forwardPdfW, out float reversePdfW)
{
    float D = GGXAnisotropicD(wm, ax, ay);
    float absDotNL = abs(cosTheta(wi));
    float absDotHL = abs(dot(wm, wi));
    float G1v = separableSmithGGXG1(wo, wm, ax, ay);
    forwardPdfW = G1v * absDotHL * D / absDotNL;
    float absDotNV = abs(cosTheta(wo));
    float absDotHV = abs(dot(wm, wo));
    float G1l = separableSmithGGXG1(wi, wm, ax, ay);
    reversePdfW = G1l * absDotHV * D / absDotNV;
}

void calcAnisotropic(const float roughness, const float anisotropic, out float ax, out float ay)
{
    const float aspect = sqrt(1.0 - 0.9 * anisotropic);
    const float r2 = roughness * roughness;
    ax = r2 / aspect;
    ay = r2 * aspect;
    return;
}

float FresnelSchlickR0FromRelativeIOR(const float relativeIOR)
{
    const float r0 = (1 - relativeIOR) / (1 + relativeIOR);
    return r0 * r0;
}

vec3 DisneyFresnel(const vec3 baseColor, const float specularTint, const float metallic, const float IOR, const float relativeIOR, const vec3 wo, const vec3 wm, const vec3 wi)
{
    float dotHV = abs(dot(wm, wo));

    vec3 tint = calculateTint(baseColor);

    // -- See section 3.1 and 3.2 of the 2015 PBR presentation + the Disney BRDF explorer (which does their
    // -- 2012 remapping rather than the SchlickR0FromRelativeIOR seen here but they mentioned the switch in 3.2).
    vec3 R0 = FresnelSchlickR0FromRelativeIOR(relativeIOR) * mix(vec3(1.0), tint, specularTint);
    R0 = mix(R0, baseColor, metallic);

    float dielectricFresnel = FresnelDielectric(dotHV, 1.0, IOR);
    vec3 metallicFresnel = schlickVec3(R0, dot(wi, wm));

    return mix(vec3(dielectricFresnel), metallicFresnel, metallic);
}

vec3 EvaluateDisneyBRDF(const DisneyMaterial mat, const vec3 wo, const vec3 wm,
                                 const vec3 wi, out float fPdf, out float rPdf)
{
    fPdf = 0.0;
    rPdf = 0.0;

    float dotNL = cosTheta(wi);
    float dotNV = cosTheta(wo);
    if(dotNL <= 0.0 || dotNV <= 0.0) 
    {
        return vec3(0.0);
    }

    vec2 a;
    calcAnisotropic(mat.roughness, mat.anisotropic, a.x, a.y);

    float d = GGXAnisotropicD(wm, a.x, a.y);
    float gl = separableSmithGGXG1(wi, wm, a.x, a.y);
    float gv = separableSmithGGXG1(wo, wm, a.x, a.y);

    vec3 f = DisneyFresnel(mat.baseColor, mat.specularTint, mat.metallic, mat.ior, mat.relativeIOR, wo, wm, wi);

    GgxVndfAnisotropicPdfOut2(wi, wm, wo, a.x, a.y, fPdf, rPdf);
    fPdf *= (1.0 / (4.0 * abs(dot(wo, wm))));
    rPdf *= (1.0 / (4.0 * abs(dot(wi, wm))));

    return d * gl * gv * f / (4.0 * dotNL * dotNV);
}

// Specular BSDF--------------------------------
float ThinTransmissionRoughness(const float ior, const float roughness)
{
    // -- Disney scales by (.65 * eta - .35) based on figure 15 of the 2015 PBR course notes. Based on their figure
    // -- the results match a geometrically thin solid fairly well.
    return clamp((0.65 * ior - 0.35) * roughness, 0.0, 1.0);
}


vec3 EvaluateDisneySpecTransmission(const vec3 baseColor, const float relativeIor, const vec3 wo, const vec3 wm,
                                             const vec3 wi, const float ax, const float ay, const bool thin)
{
    float n2 = relativeIor * relativeIor;

    float absDotNL = abs(cosTheta(wi));
    float absDotNV = abs(cosTheta(wo));
    float dotHL = dot(wm, wi);
    float dotHV = dot(wm, wo);
    float absDotHL = abs(dotHL);
    float absDotHV = abs(dotHV);

    float d = GGXAnisotropicD(wm, ax, ay);
    float gl = separableSmithGGXG1(wi, wm, ax, ay);
    float gv = separableSmithGGXG1(wo, wm, ax, ay);

    float f = FresnelDielectric(dotHV, 1.0, 1.0 / relativeIor);

    vec3 color;
    if(thin)
    {
        color = sqrt(baseColor);
    }
    else
    {
        color = baseColor;
    }
    
    // Note that we are intentionally leaving out the 1/n2 spreading factor since for VCM we will be evaluating
    // particles with this. That means we'll need to model the air-[other medium] transmission if we ever place
    // the camera inside a non-air medium.
    const float c = (absDotHL * absDotHV) / max(EPS, absDotNL * absDotNV);
    const float tmp = dotHL + relativeIor * dotHV;
    const float t = (n2 / max(EPS, tmp * tmp));
    return color * c * t * (1.0 - f) * gl * gv * d;
}

// Diffuse BRDF--------------------------------


float EvaluateDisneyRetroDiffuse(float roughness, const vec3 wo, const vec3 wm, const vec3 wi)
{
    float dotNL = abs(cosTheta(wi));
    float dotNV = abs(cosTheta(wo));

    // square
    roughness = roughness * roughness;

    float rr = 0.5 + 2.0 * dotNL * dotNL * roughness;
    float fl = schlickWeight(dotNL);
    float fv = schlickWeight(dotNV);
        
    return rr * (fl + fv + fl * fv * (rr - 1.0));
}

float EvaluateDisneyDiffuse(const vec3 baseColor, const float flatness, const float roughness, const vec3 wo, const vec3 wm,
                                   const vec3 wi, bool thin)
{
    float dotNL = abs(cosTheta(wi));
    float dotNV = abs(cosTheta(wo));

    float fl = pow((1.0 - dotNL), 5);
    float fv = pow((1.0 - dotNV), 5);

    float hanrahanKrueger = 0.0;

    if(thin && flatness > 0.0) 
    {
        float roughness = roughness * roughness;

        float dotHL = dot(wm, wi);
        float fss90 = dotHL * dotHL * roughness;
        float fss = mix(1.0, fss90, fl) * mix(1.0, fss90, fv);

        float ss = 1.25 * (fss * (1.0 / (dotNL + dotNV) - 0.5) + 0.5);
        hanrahanKrueger = ss;
    }

    float lambert = 1.0;
    // what
    float retro = EvaluateDisneyRetroDiffuse(roughness, wo, wm, wi);
    float subsurfaceApprox = mix(lambert, hanrahanKrueger, thin ? flatness : 0.0);

    return M_INVPI * (retro + subsurfaceApprox * (1.0 - 0.5 * fl) * (1.0 - 0.5 * fv));
}

// evaluate --------------------------------

void CalculateLobePdfs(const DisneyMaterial mat,
                              out float pSpecular, out float pDiffuse, out float pClearcoat, out float pSpecTrans)
{
    float metallicBRDF   = mat.metallic;
    float specularBSDF   = (1.0 - mat.metallic) * mat.specTrans;
    float dielectricBRDF = (1.0 - mat.specTrans) * (1.0 - mat.metallic);

    float specularWeight     = metallicBRDF + dielectricBRDF;
    float transmissionWeight = specularBSDF;
    float diffuseWeight      = dielectricBRDF;
    float clearcoatWeight    = 1.0 * clamp(mat.clearcoat, 0.0, 1.0); 

    float norm = clamp(1.0 / (specularWeight + transmissionWeight + diffuseWeight + clearcoatWeight), EPS, INFTY);

    pSpecular  = specularWeight     * norm;
    pSpecTrans = transmissionWeight * norm;
    pDiffuse   = diffuseWeight      * norm;
    pClearcoat = clearcoatWeight    * norm;
}

vec3 EvaluateDisneyBSDF(const DisneyMaterial mat, vec3 v, vec3 l, vec3 normal, bool thin,
                      out float forwardPdf, out float reversePdf)
{
    // calc tangent space
    vec3 T;
    vec3 B;
    basis(normal, T, B);

    vec3 wo = toLocal(T, normal, B, v);//normalize(MatrixMultiply(v, surface.worldToTangent));
    vec3 wi = toLocal(T, normal, B, l);//normalize(MatrixMultiply(l, surface.worldToTangent));
    vec3 wm = normalize(wo + wi);

    float dotNV = cosTheta(wo);
    float dotNL = cosTheta(wi);

    vec3 reflectance = vec3(0.0);
    forwardPdf = 0.0;
    reversePdf = 0.0;

    float pBRDF, pDiffuse, pClearcoat, pSpecTrans;
    CalculateLobePdfs(mat, pBRDF, pDiffuse, pClearcoat, pSpecTrans);

    const vec3 baseColor = mat.baseColor;
    const float metallic = mat.metallic;
    const float specTrans = mat.specTrans;
    const float roughness = mat.roughness;

    // calculate all of the anisotropic params
    float ax, ay;
    calcAnisotropic(mat.roughness, mat.anisotropic, ax, ay);

    float diffuseWeight = (1.0 - metallic) * (1.0 - specTrans);
    float transWeight   = (1.0 - metallic) * specTrans;

    // -- Clearcoat
    bool upperHemisphere = dotNL > 0.0 && dotNV > 0.0;
    if(upperHemisphere && mat.clearcoat > 0.0)
    {
        float forwardClearcoatPdfW;
        float reverseClearcoatPdfW;

        float clearcoat = EvaluateDisneyClearcoat(mat.clearcoat, mat.clearcoatGloss, wo, wm, wi,
                                                  forwardClearcoatPdfW, reverseClearcoatPdfW);
        reflectance += vec3(clearcoat);
        forwardPdf += pClearcoat * forwardClearcoatPdfW;
        reversePdf += pClearcoat * reverseClearcoatPdfW;
    }

    // -- Diffuse
    if(diffuseWeight > 0.0) 
    {
        float forwardDiffusePdfW = abs(cosTheta(wi));
        float reverseDiffusePdfW = abs(cosTheta(wo));
        float diffuse = EvaluateDisneyDiffuse(baseColor, mat.flatness, mat.roughness, wo, wm, wi, thin);

        vec3 sheen = EvaluateSheen(baseColor, mat.sheen, mat.sheenTint, wo, wm, wi);

        reflectance += diffuseWeight * (diffuse * baseColor + sheen);

        forwardPdf += pDiffuse * forwardDiffusePdfW;
        reversePdf += pDiffuse * reverseDiffusePdfW;
    }

    // -- transmission
    if(transWeight > 0.0) 
    {

        // Scale roughness based on IOR (Burley 2015, Figure 15).
        float rscaled = thin ? ThinTransmissionRoughness(mat.ior, mat.roughness) : mat.roughness;
        float tax, tay;
        calcAnisotropic(rscaled, mat.anisotropic, tax, tay);

        vec3 transmission = EvaluateDisneySpecTransmission(mat.baseColor, mat.relativeIOR, wo, wm, wi, tax, tay, thin);
        reflectance += transWeight * transmission;

        float forwardTransmissivePdfW;
        float reverseTransmissivePdfW;
        GgxVndfAnisotropicPdfOut2(wi, wm, wo, tax, tay, forwardTransmissivePdfW, reverseTransmissivePdfW);

        float dotLH = dot(wm, wi);
        float dotVH = dot(wm, wo);
        float v1 = dotLH + mat.relativeIOR * dotVH;
        float v2 = dotVH + mat.relativeIOR * dotLH;

        forwardPdf += pSpecTrans * forwardTransmissivePdfW / (v1 * v1);
        reversePdf += pSpecTrans * reverseTransmissivePdfW / (v2 * v2);
    }

    // -- specular
    if(upperHemisphere) 
    {
        float forwardMetallicPdfW;
        float reverseMetallicPdfW;
        vec3 specular = EvaluateDisneyBRDF(mat, wo, wm, wi, forwardMetallicPdfW, reverseMetallicPdfW);
        
        reflectance += specular;
        forwardPdf += pBRDF * forwardMetallicPdfW / (4. * abs(dot(wo, wm)));
        reversePdf += pBRDF * reverseMetallicPdfW / (4. * abs(dot(wi, wm)));
    }

    reflectance *= abs(dotNL);

    return reflectance;
}

// sample----------------------------------------------
vec3 SampleGgxVndfAnisotropic(const vec3 wo, float ax, float ay, float u1, float u2)
{
    // -- Stretch the view vector so we are sampling as though roughness==1
    vec3 v = normalize(vec3(wo.x * ax, wo.y, wo.z * ay));
    // -- Build an orthonormal basis with v, t1, and t2
    vec3 t1 = (v.y < 0.9999) ? normalize(cross(v, vec3(0.0, 1.0, 0.0))) : vec3(1.0, 0.0, 0.0);
    vec3 t2 = cross(t1, v);
    // -- Choose a point on a disk with each half of the disk weighted proportionally to its projection onto direction v
    float a = 1.0 / (1.0 + v.y);
    float r = sqrt(u1);
    float phi = (u2 < a) ? (u2 / a) * M_PI : M_PI + (u2 - a) / (1.0 - a) * M_PI;
    float p1 = r * cos(phi);
    float p2 = r * sin(phi) * ((u2 < a) ? 1.0 : v.y);
    // -- Calculate the normal in this stretched tangent space
    vec3 n = p1 * t1 + p2 * t2 + sqrt(max(0.0, 1.0 - p1 * p1 - p2 * p2)) * v;
    // -- unstretch and normalize the normal
    return normalize(vec3(ax * n.x, n.y, ay * n.z));
}

bool SampleDisneySpecTransmission(const DisneyMaterial mat, vec3 wo, bool thin, out BSDFSample sample_out, inout uint prngState)
{
    // tangent space
    //vec3 wo = MatrixMultiply(v, surface.worldToTangent);

    if(cosTheta(wo) == 0.0) 
    {
        sample_out.forwardPdfW = 0.0;
        sample_out.reversePdfW = 0.0;
        sample_out.f = vec3(0.0);
        sample_out.wi = vec3(0.0);
        return false;
    }

    // -- Scale roughness based on IOR
    float rscaled = thin ? ThinTransmissionRoughness(mat.ior, mat.roughness) : mat.roughness;
    
    float tax, tay;
    calcAnisotropic(rscaled, mat.anisotropic, tax, tay);
    
    // -- Sample visible distribution of normals
    const float r0 = stepAndOutputRNGFloat(prngState);
    const float r1 = stepAndOutputRNGFloat(prngState);
    vec3 wm = SampleGgxVndfAnisotropic(wo, tax, tay, r0, r1);

    float dotVH = dot(wo, wm);
    if(wm.y < 0.0) 
    {
        dotVH = -dotVH;
    }

    float ni = wo.y > 0.0 ? 1.0 : mat.ior;
    float nt = wo.y > 0.0 ? mat.ior : 1.0;
    float relativeIOR = ni / nt;

    // -- Disney uses the full dielectric Fresnel equation for transmission. We also importance sample F
    // -- to switch between refraction and reflection at glancing angles.
    float F = FresnelDielectric(dotVH, 1.0, mat.ior);
    
    // -- Since we're sampling the distribution of visible normals the pdf cancels out with a number of other terms.
    // -- We are left with the weight G2(wi, wo, wm) / G1(wi, wm) and since Disney uses a separable masking function
    // -- we get G1(wi, wm) * G1(wo, wm) / G1(wi, wm) = G1(wo, wm) as our weight.
    float G1v = separableSmithGGXG1(wo, wm, tax, tay);

    float pdf;

    vec3 wi;
    if(stepAndOutputRNGFloat(prngState) <= F) 
    {
        wi = normalize(reflect(-wo, wm));

        sample_out.flags = BSDF_FLAGS_DIFFUSE_TRANSMISSION;
        sample_out.f = G1v * mat.baseColor;

        float jacobian = (4. * abs(dot(wo, wm)));
        pdf = F / jacobian;
    }
    else 
    {
        if(thin) 
        {
            // -- When the surface is thin so it refracts into and then out of the surface during this shading event.
            // -- So the ray is just reflected then flipped and we use the sqrt of the surface color.
            wi = reflect(-wo, wm);
            wi.y = -wi.y;
            sample_out.f = G1v * sqrt(mat.baseColor);

            // -- Since this is a thin surface we are not ending up inside of a volume so we treat this as a scatter event.
            sample_out.flags = BSDF_FLAGS_DIFFUSE_REFLECTION;
        }
        else 
        {
            if(Transmit(wm, wo, relativeIOR, wi)) 
            {
                sample_out.flags = BSDF_FLAGS_SPECULAR_TRANSMISSION;
                // TODO: currently ignore medium
                //sample_out.medium.phaseFunction = dotVH > 0.0 ? MediumPhaseFunction::eIsotropic : MediumPhaseFunction::eVacuum;
                //sample_out.medium.extinction = CalculateExtinction(surface.transmittanceColor, surface.scatterDistance);
            }
            else 
            {
                sample_out.flags = BSDF_FLAGS_DIFFUSE_REFLECTION;
                wi = reflect(-wo, wm);
            }

            sample_out.f = G1v * mat.baseColor;
        }

        wi = normalize(wi);
        
        float dotLH = abs(dot(wi, wm));
        float tmp = dotLH + mat.relativeIOR * dotVH;
        float jacobian = dotLH / (tmp * tmp);
        pdf = (1.0 - F) / jacobian;
    }

    if(cosTheta(wi) == 0.0)
    {
        sample_out.forwardPdfW = 0.0;
        sample_out.reversePdfW = 0.0;
        sample_out.f = vec3(0.0);
        sample_out.wi = vec3(0.0);
        return false;
    }

    if(mat.roughness < 0.01) 
    {
        // -- This is a hack to allow us to sample the correct IBL texture when a path bounced off a smooth surface.
        sample_out.flags |= BSDF_FLAGS_SPECULAR_REFLECTION;
    }

    // -- calculate VNDF pdf terms and apply Jacobian and Fresnel sampling adjustments
    GgxVndfAnisotropicPdfOut2(wi, wm, wo, tax, tay, sample_out.forwardPdfW, sample_out.reversePdfW);
    sample_out.forwardPdfW *= pdf;
    sample_out.reversePdfW *= pdf;

    // -- convert wi back to world space
    // tangent space
    sample_out.wi = wi;//normalize(MatrixMultiply(wi, MatrixTranspose(surface.worldToTangent)));

    return true;
}

vec3 SampleCosineWeightedHemisphere(const float r0, const float r1)
{
    float r = sqrt(r0);
    float theta = M_PI2 * r1;

    return vec3(r * cos(theta), sqrt(max(0.0, 1 - r0)), r * sin(theta));
}

bool SampleDisneyDiffuse(const DisneyMaterial mat, vec3 wo, bool thin,
                                out BSDFSample sample_out, inout uint prngState)
{
    // tangent space
    //vec3 wo = MatrixMultiply(v, surface.worldToTangent);

    float sign = sgn(cosTheta(wo));

    // -- Sample cosine lobe
    float r0 = stepAndOutputRNGFloat(prngState);
    float r1 = stepAndOutputRNGFloat(prngState);
    //vec3 wi = sign * randomCosDirection(prngState);
    vec3 wi = sign * SampleCosineWeightedHemisphere(r0, r1);
    vec3 wm = normalize(wi + wo);

    float dotNL = cosTheta(wi);
    if(dotNL == 0.0) 
    {
        sample_out.forwardPdfW = 0.0;
        sample_out.reversePdfW = 0.0;
        sample_out.f = vec3(0.0);
        sample_out.wi = vec3(0.0);
        return false;
    }

    float dotNV = cosTheta(wo);

    float pdf;

    vec3 color = mat.baseColor;

    float p = stepAndOutputRNGFloat(prngState);

    if(p <= mat.diffTrans) 
    {
        wi = -wi;
        pdf = mat.diffTrans;

        if(thin)
        {
            color = sqrt(color);
        }
        else 
        {
            // TODO: currently skip medium
            //sample_out.medium.phaseFunction = MediumPhaseFunction::eIsotropic;
            //sample_out.medium.extinction = CalculateExtinction(surface.transmittanceColor, surface.scatterDistance);
        }
    }
    else 
    {
        pdf = (1.0 - mat.diffTrans);
    }

    vec3 sheen = EvaluateSheen(mat.baseColor, mat.sheen, mat.sheenTint, wo, wm, wi);

    float diffuse = EvaluateDisneyDiffuse(mat.baseColor, mat.flatness, mat.roughness, wo, wm, wi, thin);

    pdf = max(EPS, pdf);

    sample_out.f = sheen + color * (diffuse / pdf);
    // tangent space
    sample_out.wi = wi;//normalize(MatrixMultiply(wi, MatrixTranspose(surface.worldToTangent)));
    sample_out.forwardPdfW = abs(dotNL) * pdf;
    sample_out.reversePdfW = abs(dotNV) * pdf;
    sample_out.flags = BSDF_FLAGS_DIFFUSE_REFLECTION;

    return true;
}

bool SampleDisneyClearcoat(const DisneyMaterial mat, const vec3 wo,
                                  out BSDFSample sample_out, inout uint prngState)
{
    // tangent space
    //vec3 wo = normalize(MatrixMultiply(v, surface.worldToTangent));

    float a = 0.25;
    float a2 = a * a;

    float r0 = stepAndOutputRNGFloat(prngState);
    float r1 = stepAndOutputRNGFloat(prngState);
    float c = sqrt(max(0.0, (1.0 - pow(a2, 1.0 - r0)) / (1.0 - a2)));
    float s = sqrt(max(0.0, 1.0 - c * c));
    float phi = M_PI2 * r1;

    vec3 wm = vec3(s * cos(phi), c, s * sin(phi));
    if(dot(wm, wo) < 0.0) 
    {
        wm = -wm;
    }

    vec3 wi = reflect(-wo, wm);
    if(dot(wi, wo) < 0.0)
    {
        return false;
    }

    float clearcoatWeight = mat.clearcoat;
    float clearcoatGloss = mat.clearcoatGloss;

    float dotNH = cosTheta(wm);
    float dotLH = dot(wm, wi);
    float absDotNL = abs(cosTheta(wi));
    float absDotNV = abs(cosTheta(wo));

    float d = D_GTR1(abs(dotNH), mix(0.1, 0.001, clearcoatGloss));
    float f = schlick(0.04, dotLH);
    float g = separableSmithGGXG1(wi, 0.25) * separableSmithGGXG1(wo, 0.25);

    float fPdf = d / (4.0 * dot(wo, wm));

    sample_out.f = vec3(0.25 * clearcoatWeight * g * f * d) / fPdf;
    // tangent space
    sample_out.wi = wi;//Normalize(MatrixMultiply(wi, MatrixTranspose(surface.worldToTangent)));
    sample_out.forwardPdfW = fPdf;
    sample_out.reversePdfW = d / (4.0 * dot(wi, wm));

    return true;
}

bool SampleDisneyBRDF(const DisneyMaterial mat, const vec3 wo,
                                  out BSDFSample sample_out, inout uint prngState)
{
    // tangent space
    //vec3 wo = Normalize(MatrixMultiply(v, surface.worldToTangent));

    // -- Calculate Anisotropic params
    float ax, ay;
    calcAnisotropic(mat.roughness, mat.anisotropic, ax, ay);

    // -- Sample visible distribution of normals
    float r0 = stepAndOutputRNGFloat(prngState);
    float r1 = stepAndOutputRNGFloat(prngState);
    vec3 wm = SampleGgxVndfAnisotropic(wo, ax, ay, r0, r1);

    // -- Reflect over wm
    vec3 wi = normalize(reflect(-wo, wm));
    if(cosTheta(wi) < EPS)
    {
        sample_out.forwardPdfW = 0.0;
        sample_out.reversePdfW = 0.0;
        sample_out.f = vec3(INFTY, 0.0, 0.0);
        sample_out.wi = DisneyFresnel(mat.baseColor, mat.specularTint, mat.metallic, mat.ior, mat.relativeIOR, wo, wm, wi);
        return false;
    }

    // -- Fresnel term for this lobe is complicated since we're blending with both the metallic and the specularTint
    // -- parameters plus we must take the IOR into account for dielectrics
    vec3 F = DisneyFresnel(mat.baseColor, mat.specularTint, mat.metallic, mat.ior, mat.relativeIOR, wo, wm, wi);

    // -- Since we're sampling the distribution of visible normals the pdf cancels out with a number of other terms.
    // -- We are left with the weight G2(wi, wo, wm) / G1(wi, wm) and since Disney uses a separable masking function
    // -- we get G1(wi, wm) * G1(wo, wm) / G1(wi, wm) = G1(wo, wm) as our weight.
    float G1v = separableSmithGGXG1(wo, wm, ax, ay);
    vec3 specular = G1v * F;

    sample_out.flags = BSDF_FLAGS_DIFFUSE_REFLECTION;
    sample_out.f = specular;
    // tangent space
    sample_out.wi = wi;//Normalize(MatrixMultiply(wi, MatrixTranspose(surface.worldToTangent)));
    GgxVndfAnisotropicPdfOut2(wi, wm, wo, ax, ay, sample_out.forwardPdfW, sample_out.reversePdfW);

    sample_out.forwardPdfW *= (1.0 / (4. * abs(dot(wo, wm))));
    sample_out.reversePdfW *= (1.0 / (4. * abs(dot(wi, wm))));

    return true;
}

BSDFSample sampleDisneyBSDF(const DisneyMaterial mat, const vec3 x, const vec3 normal, bool thin, inout DisneyBSDFState state, inout uint prngState)
{
    // init
    BSDFSample ret;
    ret.f = mat.baseColor;
    ret.forwardPdfW = 1.0;
    ret.reversePdfW = 1.0;
    ret.flags = 0;

    float pSpecular;
    float pDiffuse;
    float pClearcoat;
    float pTransmission;
    CalculateLobePdfs(mat, pSpecular, pDiffuse, pClearcoat, pTransmission);

    bool success = false;

    // calc tangent space
    vec3 T, B;
    basis(normal, T, B);

    vec3 wo = normalize(toLocal(T, normal, B, x));

    float pLobe = 0.0;
    float p = stepAndOutputRNGFloat(prngState);

    
    if(p <= pSpecular) 
    {
        success = SampleDisneyBRDF(mat, wo, ret, prngState);
        pLobe = pSpecular;
    }
    else if(p > pSpecular && p <= (pSpecular + pClearcoat)) 
    {
        success = SampleDisneyClearcoat(mat, wo, ret, prngState);
        pLobe = pClearcoat;
    }
    else if(p > pSpecular + pClearcoat && p <= (pSpecular + pClearcoat + pDiffuse)) 
    {
        success = SampleDisneyDiffuse(mat, wo, thin, ret, prngState);
        pLobe = pDiffuse;
    }
    else if(pTransmission >= 0.0) 
    {
        success = SampleDisneySpecTransmission(mat, wo, thin, ret, prngState);
        pLobe = pTransmission;
    }
    else 
    {
        // -- Make sure we notice if this is occurring.
        ret.f = vec3(INFTY);
        ret.forwardPdfW = EPS;
        ret.reversePdfW = EPS;
    }

    // DEBUG
    //success = SampleDisneyBRDF(mat, wo, ret, prngState);
    //pLobe = pSpecular;

    //success = SampleDisneyClearcoat(mat, wo, ret, prngState);
    //pLobe = pClearcoat;

    //success = SampleDisneyDiffuse(mat, wo, thin, ret, prngState);
    //pLobe = pDiffuse;

    //success = SampleDisneySpecTransmission(mat, wo, thin, ret, prngState);
    //pLobe = pTransmission;

    if(pLobe > 0.0)
    {
        ret.f *= (1.0 / pLobe);
        ret.forwardPdfW *= pLobe;
        ret.reversePdfW *= pLobe;
    }

    if (!success)
    {
        ret.f = vec3(INFTY, 0.0, INFTY);
        ret.flags = BSDF_FLAGS_FAILED;
    }

    // back to world space
    ret.wi = normalize(toWorld(T, normal, B, ret.wi));

    return ret;
}

#endif