module Material;

import "../Utility/Frame";
import "../Utility/Warp";
import "../Utility/SurfaceInteraction";

__include Lambert;

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
    float4 albedo;
    float roughness;
    float metalness;
    float flatness;
    float specularTint;

    float sheen;
    float3 sheenTint;

    float anisotropic;
    float clearcoat;
    float clearcoatGloss;
    float IOR;

    int32_t albedoTexIndex;
    int32_t roughnessTexIndex;
    int32_t metalnessTexIndex;
    int32_t normalmapTexIndex;

    int32_t type;
    float3 emissive;
}

public struct BSDFContext
{
    bool isRefracted;
    bool hasBeenRefracted;
    float lastIOR;
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

    property MaterialParams params{ get; }
}

public interface IBSDF
{
    public static BSDFSample sample(BSDFContext ctx, SurfaceInteraction si, float sample1, float2 sample2);

    public static float pdf(BSDFContext ctx, SurfaceInteraction si, float3 wo);

    public static float3 eval(BSDFContext ctx, SurfaceInteraction si, float3 wo);
}

public struct DynamicMaterial : IMaterial
{

    public __init(MaterialParams params_)
    {
        params = params_;
    }

    public struct BSDF: IBSDF
    {
        public BSDFSample sample(BSDFContext ctx, SurfaceInteraction si, float sample1, float2 sample2)
        {
            switch (type)
            {
                case MaterialType::Lambert:
                    Lambert.BSDF.sample(ctx, si, sample1, sample2);
                    break;
                case MaterialType::Principle:
                    // Principle.BSDF.sample(ctx, si, sample1, sample2);
                    break;
                default: // invalid
            }

            return (BSDFSample){};
        }

        public float pdf(BSDFContext ctx, SurfaceInteraction si, float3 wo)
        {
            switch (type)
            {
                case MaterialType::Lambert:
                    Lambert.BSDF.pdf(ctx, si, wo);
                    break;
                case MaterialType::Principle:
                    // Principle.BSDF.pdf(ctx, si, wo);
                    break;
                default: // invalid
            }
        }

        public float3 eval(BSDFContext ctx, SurfaceInteraction si, float3 wo)
        {
            switch (type)
            {
                case MaterialType::Lambert:
                    Lambert.BSDF.eval(ctx, si, wo);
                    break;
                case MaterialType::Principle:
                    // Principle.BSDF.eval(ctx, si, wo);
                    break;
                default: // invalid
            }
        }
    }    

    public property MaterialType type
    {
        get{MaterialType(params.type);}
    }

    MaterialParams params;
}

