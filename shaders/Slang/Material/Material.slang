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
__include Principle;

// **always synchronize with CPU side**
public enum MaterialType
{
    Lambert = 0,
    Principle = 1,
    MaterialTypeNum,
}

// all parameters bound from CPU, **always synchronize with CPU side**
public struct MaterialParams // std140
{
    public __init() {}

    public float3 albedo = k::zeros.xyz;
    public float roughness = 0.0;

    public float metallic = 0.0;
    public float specTrans = 0.0;
    public float diffTrans = 0.0;
    public float flatness = 0.0;

    public float specularTint = 0.0;
    public float3 padding = k::zeros.xyz;

    public float sheen = 0.0;
    public float3 sheenTint = k::zeros.xyz;

    public float anisotropic    = 0.0;
    public float clearcoat      = 0.0;
    public float clearcoatGloss = 0.0;
    public float IOR            = 1.0;

    public int32_t albedoTexIndex    = k::invalidTexIndex;
    public int32_t roughnessTexIndex = k::invalidTexIndex;
    public int32_t metalnessTexIndex = k::invalidTexIndex;
    public int32_t normalmapTexIndex = k::invalidTexIndex;

    public int32_t type = MaterialType::Principle;
    public float3 emissive = k::zeros.xyz;
}

public struct BSDFContext
{
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

    public float3 f;
    public float3 wo;
    public float pdf;
    public Flags flags;
}

public interface IMaterial 
{
    public associatedtype BSDF : IBSDF;

    public __init(in MaterialParams params);
}

public interface IBSDF
{
    public static Optional<BSDFSample> sample(MaterialParams params, inout BSDFContext ctx, SurfaceInteraction si, float sample1, float2 sample2);

    public static float pdf(MaterialParams params, inout BSDFContext ctx, SurfaceInteraction si, float3 wo);

    public static float3 eval(MaterialParams params, inout BSDFContext ctx, SurfaceInteraction si, float3 wo);
}

public struct DynamicMaterial : IMaterial
{

    public __init(MaterialParams params_)
    {

    }

    public struct BSDF: IBSDF
    {
        public static Optional<BSDFSample> sample(MaterialParams params, inout BSDFContext ctx, SurfaceInteraction si, float sample1, float2 sample2)
        {
            switch (params.type)
            {
                case MaterialType::Lambert:
                    return Lambert.BSDF.sample(params, ctx, si, sample1, sample2);
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
            case MaterialType::Principle:
                    return Principle.BSDF.eval(params, ctx, si, wo);
                default: // invalid
            }

            return k::zeros.xyz;
        }
    }    
}

