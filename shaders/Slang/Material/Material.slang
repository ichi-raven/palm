module Material;

import "../Utility/Frame";
import "../Utility/Warp";
import "../Utility/Constants";
import "../Utility/SurfaceInteraction";

/*
When you want to add a material (BSDF)
1. __include it
2. add it to MaterialType (don't forget to add to CPU side)
3. If you add MaterialParams, don't forget to add it to the CPU side as well.
   Add switch-case statement for DynamicMaterial (BAD)
*/

__include Lambert;
__include Conductor;
__include Dielectric;
__include Principle;

// **always synchronize with CPU side**
public enum MaterialType
{
    Lambert     = 0,
    Conductor   = 1,
    Dielectric  = 2,
    Principle   = 3,
    MaterialTypeNum,
}

// all parameters bound from CPU, **always synchronize with CPU side**
public struct MaterialParams // std140
{
    public __init() {}

    public static MaterialParams loadWithTextures(MaterialParams params, Texture2D<float4> textures[], SamplerState sampler, const float2 uv)
    {
        if (params.albedoTexIndex != k::invalidTexIndex)
        {
            params.albedo = textures[NonUniformResourceIndex(params.albedoTexIndex)].Sample(sampler, uv).xyz;
        }
        // if (params.roughnessTexIndex != k::invalidTexIndex)
        // {
        //     params.roughness = textures[NonUniformResourceIndex(params.roughnessTexIndex)].Sample(sampler, uv).x;
        // }
        // if (params.metalnessTexIndex != k::invalidTexIndex)
        // {
        //     params.metallic = textures[NonUniformResourceIndex(params.metalnessTexIndex)].Sample(sampler, uv).x;
        // }

        if (params.normalmapTexIndex != k::invalidTexIndex)
        {
            // TODO:
        }

        return params;
    }

    public float3 albedo = k::zeros.xyz;
    public float roughness = 0.0;

    public float metallic = 0.0;
    public float specTrans = 0.0;
    public float diffTrans = 0.0;
    public float flatness = 0.0;

    public float3 padding = k::zeros.xyz;
    public float specularTint = 0.0;

    public float3 sheenTint = k::zeros.xyz;
    public float sheen = 0.0;

    public float anisotropic    = 0.0;
    public float clearcoat      = 0.0;
    public float clearcoatGloss = 0.0;
    public float IOR            = 1.0;

    public int32_t albedoTexIndex    = k::invalidTexIndex;
    public int32_t roughnessTexIndex = k::invalidTexIndex;
    public int32_t metalnessTexIndex = k::invalidTexIndex;
    public int32_t normalmapTexIndex = k::invalidTexIndex;

    public float3 emissive = k::zeros.xyz;
    public int32_t type = MaterialType::Principle;
}

public struct BSDFContext
{
    public __init()
    {
        isRefracted = false;
        hasBeenRefracted = false;
        lastIOR = 1.0;
    }

    [mutating]
    public void update(BSDFSample sample, const float lastIOR_)
    {
        isRefracted = sample.isTransmitted();
        hasBeenRefracted |= isRefracted;
        lastIOR = lastIOR_;
    }

    public bool isRefracted;
    public bool hasBeenRefracted;
    public float lastIOR;
}

public struct BSDFSample
{
    [Flags]
    public enum Flags
    {
        Reflection,    //  = 1
        Transmission,  //  = 2
        Diffuse,       //  = 4
        Glossy,        //  = 8
        Specular,      //  = 16
    }

    public bool isSpecular()
    {
        return flags & Flags.Specular;
    }

    public bool isTransmitted()
    {
        return flags & Flags.Transmission;
    }

    public float3 f;
    public float3 wo;
    public float pdf;
    public Flags flags;
}

public interface IMaterial 
{
    public associatedtype BSDF : IBSDF;
}

public interface IBSDF
{
    public static Optional<BSDFSample> sample(MaterialParams params, inout BSDFContext ctx, SurfaceInteraction si, float sample1, float2 sample2);

    public static float pdf(MaterialParams params, inout BSDFContext ctx, SurfaceInteraction si, float3 wo);

    public static float3 eval(MaterialParams params, inout BSDFContext ctx, SurfaceInteraction si, float3 wo);
}

public struct DynamicMaterial : IMaterial
{
    
    public struct BSDF: IBSDF
    {
        public static Optional<BSDFSample> sample(MaterialParams params, inout BSDFContext ctx, SurfaceInteraction si, float sample1, float2 sample2)
        {
            switch (params.type)
            {
                case MaterialType::Lambert:
                    return Lambert.BSDF.sample(params, ctx, si, sample1, sample2);
                case MaterialType::Conductor:
                    params.metallic = 1.0;
                    return Conductor.BSDF.sample(params, ctx, si, sample1, sample2);
                case MaterialType::Dielectric:
                    return Dielectric.BSDF.sample(params, ctx, si, sample1, sample2);
                case MaterialType::Principle:
                    return Principle.BSDF.sample(params, ctx, si, sample1, sample2);
                default: // invalid
            }

            return none;
        }

        public static float pdf(MaterialParams params, inout BSDFContext ctx, SurfaceInteraction si, float3 wo)
        {
            switch (params.type)
            {
                case MaterialType::Lambert:
                    return Lambert.BSDF.pdf(params, ctx, si, wo);
                case MaterialType::Conductor:
                    params.metallic = 1.0;
                    return Conductor.BSDF.pdf(params, ctx, si, wo);
                case MaterialType::Dielectric:
                    return Dielectric.BSDF.pdf(params, ctx, si, wo);
                case MaterialType::Principle:
                    return Principle.BSDF.pdf(params, ctx, si, wo);
                default: // invalid
            }

            return 0.0;
        }

        public static float3 eval(MaterialParams params, inout BSDFContext ctx, SurfaceInteraction si, float3 wo)
        {
            switch (params.type)
            {
                case MaterialType::Lambert:
                    return Lambert.BSDF.eval(params, ctx, si, wo);
                case MaterialType::Conductor:
                    params.metallic = 1.0;
                    return Conductor.BSDF.eval(params, ctx, si, wo);
                case MaterialType::Dielectric:
                    return Dielectric.BSDF.eval(params, ctx, si, wo);
                case MaterialType::Principle:
                    return Principle.BSDF.eval(params, ctx, si, wo);
                default: // invalid
            }

            return k::zeros.xyz;
        }
    }    
}

