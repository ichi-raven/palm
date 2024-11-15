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

public enum Type
{
    Point = 0,
    Area = 1,
    Infinite = 2,
} 

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
            v0.pos    = mul(instance.world, float4(v0_.pos, 1.0)).xyz;
            v0.normal = normalize(mul(instance.worldInvTrans, float4(v0_.normal, 1.)).xyz);
            v1.pos    = mul(instance.world, float4(v1_.pos, 1.0)).xyz;
            v1.normal = normalize(mul(instance.worldInvTrans, float4(v1_.normal, 1.)).xyz);
            v2.pos    = mul(instance.world, float4(v2_.pos, 1.0)).xyz;
            v2.normal = normalize(mul(instance.worldInvTrans, float4(v2_.normal, 1.)).xyz);

            area = 0.5 * length(cross(v2.pos - v0.pos, v1.pos - v0.pos));
        }

        Tuple<float3, float3> sample(float2 sample2)
        {
            if (sample2.x + sample2.y > 1.0)
            {
                sample2.x = (1.0 - sample2.x);
                sample2.y = (1.0 - sample2.y);
            }

            let w = float3(1. - (sample2.x + sample2.y), sample2.x, sample2.y);

            return makeTuple(v0.pos * w.x + v1.pos * w.y + v2.pos * w.z, v0.normal * w.x + v1.normal * w.y + v2.normal * w.z);
        }

        V v0;
        V v1;
        V v2;
        float area;
    }

    public static EmitterSample sample<V : IVertex, I : IInstance>(StructuredBuffer<EmitterParams> params, StructuredBuffer<V> vertices[], StructuredBuffer<uint32_t> indices[], StructuredBuffer<I> instances, const SurfaceInteraction si, const float sample1, const float sample12, const float2 sample2)
    {
        // TODO: should be originally sampled based on distance and area
        uint emitterCount, emitterStride;
        params.GetDimensions(emitterCount, emitterStride);
        let sampled   = params[uint32_t(emitterCount * sample1)];
        let selectPdf = 1. / float(emitterCount);

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
            // TODO: effective sampling
            let faceIndex = uint(sample12 * sampled.faceNum);

            let index = uint3(indices[sampled.meshIndex][faceIndex * 3 + 0], indices[sampled.meshIndex][faceIndex * 3 + 1], indices[sampled.meshIndex][faceIndex * 3 + 2]);
            let face  = Face<V, I>(vertices[sampled.meshIndex][index.x], vertices[sampled.meshIndex][index.y], vertices[sampled.meshIndex][index.z], instances[sampled.meshIndex]);

            let v  = face.sample(sample2);
            let on     = v._0;
            let normal = v._1;

            ret.pdf = 1.0 / sampled.faceNum / face.area;
            ret.distance = distance(si.pos, on);
            ret.emissive = sampled.emissive;
            ret.to       = on - si.pos;
            ret.normal   = normal;
            break;

        case Type::Infinite:  // TODO:
            break;
        default: // invalid
            ret.emissive = k::red;
            ret.pdf = 1.0;
            break;
        }

        ret.pdf *= selectPdf;
        return ret;
    }
}