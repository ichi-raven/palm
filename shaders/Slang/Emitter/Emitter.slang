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

public class EmitterSampler
{
    struct Face<V: IVertex>
    {
        V v0;
        V v1;
        V v2;

        property float area
        {
            get
            {
                return 0.5 * length(cross(v2.pos - v0.pos, v1.pos - v0.pos));
            }
        };

        property float3 normal
        {
            get
            {
                return 1. / 3. * (v0.normal + v1.normal + v2.normal);
            }
        }

        float3 samplePos(float2 sample2)
        {
            let w = float3(1. - (sample2.x + sample2.y), sample2.x, sample2.y);

            return (v0.pos * w.x + v1.pos * w.y + v2.pos * w.z);
        }
    }

    public static EmitterSample sample<V: IVertex>(StructuredBuffer<EmitterParams> params, const uint32_t emitterCount, StructuredBuffer<V> vertices[], StructuredBuffer<uint32_t> indices[], const SurfaceInteraction si, const float sample1, const float2 sample2)
    {
        // HACK: should be originally sampled based on distance and area.
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
            ret.normal   = -normalize(ret.to);
            break;

        case Type::Area:
            
            let faceIndex = uint(sample2.x * sampled.faceNum);

            let index = uint3(indices[sampled.meshIndex][faceIndex * 3 + 0], indices[sampled.meshIndex][faceIndex * 3 + 1], indices[sampled.meshIndex][faceIndex * 3 + 2]);
            let face = Face<V>(vertices[sampled.meshIndex][index.x], vertices[sampled.meshIndex][index.y], vertices[sampled.meshIndex][index.z]);

            let on = face.samplePos(sample2);

            ret.pdf      = 1.0 / sampled.faceNum / face.area;
            ret.distance = distance(si.pos, on);
            ret.emissive = sampled.emissive;
            ret.to       = on - si.pos;
            ret.normal   = face.normal;
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