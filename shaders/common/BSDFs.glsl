#ifndef BSDFS_GLSL_
#define BSDFS_GLSL_

precision highp float;
precision highp int;

#extension GL_EXT_buffer_reference : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_shader_explicit_arithmetic_types : enable
#extension GL_EXT_control_flow_attributes : enable

#include "types.glsl"
#include "constants.glsl"
#include "randoms.glsl"

bool isReflection(const uint flags)
{
  return bool(flags & BSDF_FLAGS_REFLECTION);
}

bool isTransMission(const uint flags)
{
  return bool(flags & BSDF_FLAGS_TRANSMISSION);
}

bool isDiffuse(const uint flags)
{
  return bool(flags & BSDF_FLAGS_DIFFUSE);
}

bool isGlossy(const uint flags)
{
  return bool(flags & BSDF_FLAGS_GLOSSY);
}

bool isSpecular(const uint flags)
{
  return bool(flags & BSDF_FLAGS_SPECULAR);
}

bool isNonSpecular(const uint flags) 
{
    return bool(flags & (BSDF_FLAGS_DIFFUSE | BSDF_FLAGS_GLOSSY)); 
}

float schlick(float cosine, float ref_idx) 
{
  float r0 = (1 - ref_idx) / (1 + ref_idx);
  r0 = r0 * r0;
  return r0 + (1 - r0) * pow((1 - cosine), 5);
}

vec3 local(ONB onb, vec3 pos)
{
  return pos.x * onb.u + pos.y * onb.v + pos.z * onb.w;
}

vec3 setFaceNormal(const vec3 rayDir, const vec3 outwardNormal)
{
  return dot(rayDir, outwardNormal) < 0 ? outwardNormal : -outwardNormal;
}

ONB buildFromW(vec3 w)
{
  ONB ret;
  ret.w = normalize(w);
  vec3 a = abs(ret.w.x) > 0.9 ? vec3(0, 1.0, 0) : vec3(1.0, 0, 0);
  ret.v = normalize(cross(ret.w, a));
  ret.u = cross(ret.w, ret.v);

  return ret;
}

vec3 lambertScatter(vec3 normal, inout uint prngState, out float pdf)
{
  ONB onb = buildFromW(normal);
  vec3 dir = local(onb, randomCosDirection(prngState));
  pdf = dot(onb.w, dir) / M_PI;
  return normalize(dir);
}

vec3 conductorScatter(vec3 wo, vec3 normal, float alpha, inout uint prngState, inout float pdf)
{
  // TODO:
  pdf = 1.0;

  return reflect(-wo, normalize(normal)) + alpha * randomUnitVector(prngState);
}

vec3 dielectricScatter(vec3 wo, vec3 normal, bool frontFace, float IOR, inout uint prngState, inout float pdf)
{
  float eta = frontFace ? 1.0 / IOR : IOR;

  const vec3 unitDir = -normalize(wo);

  const float cosine = min(dot(-unitDir, normal), 1.0);
  const float sine = sqrt(1.0 - cosine * cosine);
  const float reflectProb = schlick(cosine, eta);

  if (eta * sine > 1.0 || stepAndOutputRNGFloat(prngState) < reflectProb) 
  {
    return reflect(unitDir, normalize(normal));
  }

  // TODO:
  pdf = 1.0;
  return refract(unitDir, normalize(normal), eta);

}

BSDFSample sampleBSDF(const Material mat, const vec3 wo, const vec3 normal, inout uint prngState)
{
  BSDFSample ret;
  ret.f = mat.albedo.xyz;
  ret.forwardPdfW = 1.0;
  ret.reversePdfW = 1.0;
  ret.flags = 0;

  const vec3 faceNormal = setFaceNormal(-wo, normal);
  const bool front = faceNormal == normal;

  switch(mat.matType)
  {
    case MAT_LAMBERT:
      ret.flags = BSDF_FLAGS_DIFFUSE | BSDF_FLAGS_REFLECTION;
      ret.wi = lambertScatter(faceNormal, prngState, ret.forwardPdfW);
      ret.f *= M_INVPI * abs(dot(ret.wi, faceNormal));
    break;
    case MAT_CONDUCTOR:
      // HACK:
      ret.flags = BSDF_FLAGS_SPECULAR;
      ret.flags |= BSDF_FLAGS_REFLECTION;
      const float alpha = mat.alpha;
      ret.wi = conductorScatter(wo, faceNormal, mat.alpha, prngState, ret.forwardPdfW);
      if (dot(ret.wi, faceNormal) <= 0.0)
      {
       ret.f = vec3(0.0);
      }
    break;
    case MAT_DIELECTRIC:
      ret.flags = BSDF_FLAGS_SPECULAR;// | BSDF_FLAGS_TRANSMISSION;
      // HACK:
      //ret.flags |= mat.IOR == 1.0 ? BSDF_FLAGS_REFLECTION : 0;
      ret.wi = dielectricScatter(wo, faceNormal, front, mat.IOR, prngState, ret.forwardPdfW);
    break;
    default:
    // ERROR
    break;
  }
  
  // prevent zero-division
  ret.forwardPdfW = max(ret.forwardPdfW, EPS);

  return ret;
}

//test Disney BSDF-------------------------------------

/*

// ---------------------------------------------
// Color
// ---------------------------------------------
float luma(vec3 color) 
{
    return dot(color, vec3(0.299, 0.587, 0.114));
}

// ---------------------------------------------
// Maths
// ---------------------------------------------
vec3 cosineSampleHemisphere(vec3 n, inout uint prngState)
{
    vec2 rnd = vec2(stepAndOutputRNGFloat(prngState), stepAndOutputRNGFloat(prngState));

    float a = M_PI*2.*rnd.x;
    float b = 2.0*rnd.y-1.0;
    
    vec3 dir = vec3(sqrt(1.0-b*b)*vec2(cos(a),sin(a)),b);
    return normalize(n + dir);
}

// From Pixar - https://graphics.pixar.com/library/OrthonormalB/paper.pdf
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

// ---------------------------------------------
// Microfacet
// ---------------------------------------------
float Fresnel(float n1, float n2, float VoH, float f0, float f90)
{
    float r0 = (n1-n2) / (n1+n2);
    r0 *= r0;
    if (n1 > n2)
    {
        float n = n1/n2;
        float sinT2 = n*n*(1.0-VoH*VoH);
        if (sinT2 > 1.0)
            return f90;
        VoH = sqrt(1.0-sinT2);
    }
    float x = 1.0-VoH;
    float ret = r0+(1.0-r0)*pow(x, 5.);
    
    return mix(f0, f90, ret);
}
vec3 F_Schlick(vec3 f0, float theta) {
    return f0 + (1.-f0) * pow(1.0-theta, 5.);
}

float F_Schlick(float f0, float f90, float theta) {
    return f0 + (f90 - f0) * pow(1.0-theta, 5.0);
}

float D_GTR(float roughness, float NoH, float k) {
    float a2 = pow(roughness, 2.);
    return a2 / (M_PI * pow((NoH*NoH)*(a2*a2-1.)+1., k));
}

float SmithG(float NoV, float roughness2)
{
    float a = pow(roughness2, 2.);
    float b = pow(NoV,2.);
    return (2.*NoV) / (NoV+sqrt(a + b - a * b));
}

float GeometryTerm(float NoL, float NoV, float roughness)
{
    float a2 = roughness*roughness;
    float G1 = SmithG(NoV, a2);
    float G2 = SmithG(NoL, a2);
    return G1*G2;
}

vec3 SampleGGXVNDF(vec3 V, float ax, float ay, float r1, float r2)
{
    vec3 Vh = normalize(vec3(ax * V.x, ay * V.y, V.z));

    float lensq = Vh.x * Vh.x + Vh.y * Vh.y;
    vec3 T1 = lensq > 0. ? vec3(-Vh.y, Vh.x, 0) * inversesqrt(lensq) : vec3(1, 0, 0);
    vec3 T2 = cross(Vh, T1);

    float r = sqrt(r1);
    float phi = 2.0 * M_PI * r2;
    float t1 = r * cos(phi);
    float t2 = r * sin(phi);
    float s = 0.5 * (1.0 + Vh.z);
    t2 = (1.0 - s) * sqrt(1.0 - t1 * t1) + s * t2;

    vec3 Nh = t1 * T1 + t2 * T2 + sqrt(max(0.0, 1.0 - t1 * t1 - t2 * t2)) * Vh;

    return normalize(vec3(ax * Nh.x, ay * Nh.y, max(0.0, Nh.z)));
}

float GGXVNDFPdf(float NoH, float NoV, float roughness)
{
 	float D = D_GTR(roughness, NoH, 2.);
    float G1 = SmithG(NoV, roughness*roughness);
    return (D * G1) / max(0.00001, 4.0f * NoV);
}


vec3 evalDisneyDiffuse(DisneyMaterial mat, float NoL, float NoV, float LoH, float roughness) {
    float FD90 = 0.5 + 2. * roughness * pow(LoH,2.);
    float a = F_Schlick(1.,FD90, NoL);
    float b = F_Schlick(1.,FD90, NoV);
    
    return mat.baseColor.xyz * (a * b * M_INVPI);
}

vec3 evalDisneySpecularReflection(DisneyMaterial mat, vec3 F, float NoH, float NoV, float NoL) {
    float roughness = pow(mat.roughness, 2.);
    float D = D_GTR(roughness, NoH,2.);
    float G = GeometryTerm(NoL, NoV, pow(0.5+mat.roughness*.5,2.));

    vec3 spec = D*F*G / (4. * NoL * NoV);
    
    return spec;
}

vec3 evalDisneySpecularRefraction(DisneyMaterial mat, float F, float NoH, float NoV, float NoL, float VoH, float LoH, float eta, out float pdf) 
{
    float roughness = pow(mat.roughness, 2.);
    float D = D_GTR(roughness, NoH, 2.);
    float G = GeometryTerm(NoL, NoV, pow(0.5+mat.roughness*.5, 2.));
    float denom = max(EPS, pow(LoH + VoH * eta, 2.));

    float jacobian = abs(LoH) / denom;
    pdf = SmithG(abs(NoL), roughness*roughness) * max(0.0, VoH) * D * jacobian / max(EPS, NoV);
    
    vec3 spec = pow(1. - mat.baseColor, vec3(0.5))  * D * (1.-F) * G * abs(VoH) * jacobian * pow(eta, 2.) / max(EPS, abs(NoL * NoV));
    
    return spec;
}


BSDFSample sampleDisneyBSDF(const DisneyMaterial mat, const vec3 v, const vec3 n, inout DisneyBSDFState state, inout uint prngState)
{
  // init
  BSDFSample ret;
  ret.f = mat.baseColor;
  ret.pdf = 1.0;
  ret.flags = 0;
  ret.eta = 1.0;
  //ret.misWeight = 1.0;

    vec3 l = vec3(0.0); // ret.wi
    state.hasBeenRefracted = state.isRefracted;
    
    float roughness = pow(mat.roughness, 2.);

    // sample microfacet normal
    vec3 t,b;
    basis(n,t,b);
    vec3 V = toLocal(t,b,n,v);
    vec3 h = SampleGGXVNDF(V, roughness,roughness, stepAndOutputRNGFloat(prngState), stepAndOutputRNGFloat(prngState));
    if (h.z < 0.0)
        h = -h;
    h = toWorld(t,b,n,h);

    // fresnel
    float VoH = dot(v,h);
    vec3 f0 = mix(vec3(0.04), mat.baseColor, mat.metallic);
    vec3 F = F_Schlick(f0, VoH);
    float dielF = Fresnel(state.lastIOR, mat.ior, abs(VoH), 0., 1.);
    
    // lobe weight probability
    float diffW = (1.-mat.metallic) * (1.-mat.specTrans);
    float reflectW = luma(F);
    float refractW = (1.-mat.metallic) * (mat.specTrans) * (1.-dielF);
    float invW = 1./(diffW + reflectW + refractW);
    diffW *= invW;
    reflectW *= invW;
    refractW *= invW;
    
    // cdf
    float cdf[3];
    cdf[0] = diffW;
    cdf[1] = cdf[0] + reflectW;
    cdf[2] = cdf[1] + refractW;
    
    // if (cdf[0] + cdf[1] + cdf[2] > 1.0)
    // {
    //   ret.f = vec3(cdf[0], cdf[1], cdf[2]);
    //   ret.f = vec3(refractW);
    //   return ret;
    // }

    vec4 bsdf = vec4(0.);
    float rnd = stepAndOutputRNGFloat(prngState);
    if (rnd < cdf[0]) // diffuse
    {
        l = cosineSampleHemisphere(n, prngState);
        h = normalize(l+v);
        
        float NoL = dot(n,l);
        float NoV = dot(n,v);
        if ( NoL <= 0. || NoV <= 0. ) { return ret; }
        float LoH = dot(l,h);
        float pdf = NoL * M_INVPI;
        
        vec3 diff = evalDisneyDiffuse(mat, NoL, NoV, LoH, roughness) * (1.-F);
        bsdf.rgb = diff;
        bsdf.a = diffW * pdf;
    } 
    else if(rnd < cdf[1]) // reflection
    {
        l = reflect(-v, h);
        
        float NoL = dot(n, l);
        float NoV = dot(n, v);
        if ( NoL <= 0. || NoV <= 0. ) { return ret; }
        float NoH = min(0.99, dot(n,h));
        float pdf = GGXVNDFPdf(NoH, NoV, roughness);
        
        vec3 spec = evalDisneySpecularReflection(mat, F, NoH, NoV, NoL);
        bsdf.rgb = spec;
        bsdf.a = reflectW * pdf;
    }
    else // refraction
    {
        state.isRefracted = !state.isRefracted;
        float eta = state.lastIOR/max(EPS, mat.ior);
        l = refract(-v, h, eta);
        state.lastIOR = mat.ior;
        
        float NoL = dot(n,l);
        if ( NoL <= 0. ) { return ret; }
        float NoV = dot(n,v);
        float NoH = min(1.0 - EPS ,dot(n,h));
        float LoH = dot(l,h);
        
        float pdf = 1.0;
        vec3 spec = evalDisneySpecularRefraction(mat, dielF, NoH, NoV, NoL, VoH, LoH, eta, pdf);
        
        bsdf.rgb = spec;
        bsdf.a = refractW* pdf;
    }
    
    bsdf.rgb *= abs(dot(n,l));


  ret.f = bsdf.rgb;
  ret.wi = normalize(l);
  ret.pdf = max(EPS, bsdf.a);
  ret.flags = BSDF_FLAGS_ALL;
  ret.eta = 1.0;
  return ret;
}
*/

#endif