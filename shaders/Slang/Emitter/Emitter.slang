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

public class EmitterSampler
{
    public static EmitterSample sample(EmitterParams params[], uint32_t emitterCount, SurfaceInteraction si, float sample1, float2 sample2)
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
            ret.normal   = -normalize(es.to);
            break;

        case Type::Area:
            // have to calculate all areas

            // ret.pdf      = 1.0 / sampled.faceNum / ;
            // ret.distance = distance(si.pos, sampled.pos);
            // ret.emissive = sampled.emissive;
            // ret.to       = sampled.pos - si.pos;
            // ret.normal   = -normalize(es.to);
            break;

        case Type::Infinite:  // TODO:
            break;
        default:
            ret.
            break;
        }

        ret.pdf *= selectPdf;
        return ret;
    }
}