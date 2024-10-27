module Material;

import "../Utility/Frame";
import "../Utility/Warp";

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

    int32_t materialType;
    float3 emissive;
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

    property BSDF bsdf
    {
        get;
    }
}

public interface IBSDF
{
    public BSDFSample sample();

    public float pdf();

    public float3 eval();
}