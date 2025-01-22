module Emitter;

import "../Utility/Constants";
import "../Utility/Warp";
import "../Utility/SurfaceInteraction";

public struct EmitterSample
{
    public float pdf;
    public float3 emissive;
    public float3 to;
    public float distance;
    public float3 normal;
}

// **always synchronize with CPU side**
public enum Type
{
    Point = 0,
    Area = 1,
    Infinite = 2,
} 

// all parameters bound from CPU, **always synchronize with CPU side**
public struct EmitterParams
{
    public float3 pos = k::zeros.xyz;
    public int32_t type = Type::Point;

    public uint32_t faceNum  = 0;
    public int32_t meshIndex = -1;
    public float2 padding = k::zeros.xy;

    public float3 emissive = k::zeros.xyz;
    public int32_t texIndex = -1;
}

public interface IVertex
{
    public property float3 pos {get; set;}
    public property float3 normal {get; set;}
}

public interface IInstance
{
    public property float4x4 world { get; set; }
    public property float4x4 worldInvTrans { get; set; }
}

public class EmitterSampler
{
    struct Face<V: IVertex, I: IInstance>
    {
        __init(V v0_, V v1_, V v2_, I instance)
        {
            v0.pos    = mul(instance.world, float4(v0_.pos, 1.)).xyz;
            v0.normal = mul(instance.worldInvTrans, float4(v0_.normal, 0.)).xyz;
            v1.pos    = mul(instance.world, float4(v1_.pos, 1.)).xyz;
            v1.normal = mul(instance.worldInvTrans, float4(v1_.normal, 0.)).xyz;
            v2.pos    = mul(instance.world, float4(v2_.pos, 1.)).xyz;
            v2.normal = mul(instance.worldInvTrans, float4(v2_.normal, 0.)).xyz;

            area = 0.5 * length(cross(v2.pos - v0.pos, v1.pos - v0.pos));
        }

        V sample(float2 sample2)
        {
            if (sample2.x + sample2.y > 1.0)
            {
                sample2.x = (1.0 - sample2.x);
                sample2.y = (1.0 - sample2.y);
            }

            let w = float3(sample2.x, sample2.y, 1.0 - sample2.x - sample2.y);

            V ret;
            ret.pos = v0.pos * w.x + v1.pos * w.y + v2.pos * w.z;
            ret.normal = normalize(v0.normal * w.x + v1.normal * w.y + v2.normal * w.z);
            return ret;
        }

        V v0;
        V v1;
        V v2;
        float area;
    }

    public static EmitterSample sample<V : IVertex, I : IInstance>(StructuredBuffer<EmitterParams> params, StructuredBuffer<V> vertices[], StructuredBuffer<uint32_t> indices[], StructuredBuffer<I> instances, const SurfaceInteraction si, const float sample1, const float2 sample2, const float sample3)
    {
        // TODO: should be originally sampled based on distance and area
        uint emitterCount, emitterStride;
        params.GetDimensions(emitterCount, emitterStride);
        let sampled   = params[uint32_t(emitterCount * sample1)];
        let selectPdf = 1. / float(emitterCount); // apply after

        EmitterSample ret;

        switch (sampled.type)
        {
        case Type::Point:
            ret.pdf      = 1.0;
            ret.distance = distance(si.pos, sampled.pos);
            ret.emissive = sampled.emissive;
            ret.to       = sampled.pos - si.pos;
            ret.normal   = (-1.0 / ret.distance) * ret.to;
            break;

        case Type::Area:
            // TODO: more effective sampling
            let faceIndex = uint(sample3 * sampled.faceNum);
            let meshIndex = sampled.meshIndex;

            let index = uint3(indices[meshIndex][faceIndex * 3 + 0], indices[meshIndex][faceIndex * 3 + 1], indices[meshIndex][faceIndex * 3 + 2]);
            let face  = Face<V, I>(vertices[meshIndex][index.x], vertices[meshIndex][index.y], vertices[meshIndex][index.z], instances[meshIndex]);

            let v = face.sample(sample2);

            ret.pdf = 1.0 / (sampled.faceNum * face.area);
            ret.distance = distance(si.pos, v.pos);
            ret.emissive = sampled.emissive;
            ret.to       = v.pos - si.pos;
            ret.normal   = v.normal;
            break;

        case Type::Infinite:  // TODO:
            ret.emissive = k::black; // DEBUG:
            ret.pdf = 0.0;
            break;
        default: // invalid
            ret.emissive = k::black; // DEBUG:
            ret.pdf = 0.0;
            break;
        }

        // apply emitter selecting pdf
        ret.pdf *= selectPdf;

        return ret;
    }

    public static float pdf(const SurfaceInteraction si, const SurfaceInteraction bsdfSi, const uint32_t emitterNum)
    {
        if (emitterNum == 0)
        {
            return 0.;
        }
        
        let to = bsdfSi.pos - si.pos;
        let distSq = dot(to, to);

        let lightCos = abs(dot(normalize(to), bsdfSi.normal));

        let invG = distSq / lightCos;
        return invG / bsdfSi.area / emitterNum;
    }
}