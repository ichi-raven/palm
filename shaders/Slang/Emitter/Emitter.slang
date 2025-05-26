module Emitter;

import "../Utility/Constants";
import "../Utility/Warp";
import "../Utility/SurfaceInteraction";
import "../Sampler/Sampler";

public struct EmitterSample
{
    public float pdf        = 1.0;
    public float3 emissive  = k::black;
    public float3 to        = k::zeros.xyz;
    public float distance   = 0.0;
    public float3 normal    = float3(0., 0., 1.);
    public bool isInfinite  = false;
}

// **always synchronize with CPU side**
public enum EmitterType
{
    Point = 0,
    Area = 1,
    Infinite = 2,
} 

// all parameters bound from CPU, **always synchronize with CPU side**
public struct EmitterParams
{
    public float3 pos   = k::zeros.xyz;
    public int32_t type = EmitterType::Point;

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

    public static EmitterSample sample<V : IVertex, I : IInstance, S : ISampler>(StructuredBuffer<EmitterParams> params, StructuredBuffer<V> vertices[], StructuredBuffer<uint32_t> indices[], StructuredBuffer<I> instances, Texture2D<float4> textures[], SamplerState texSampler, const SurfaceInteraction si, inout S sampler)
    {
        let sample1 = sampler.next1D();
        let sample2 = sampler.next2D();
        let sample3 = sampler.next1D();

        // TODO: should be originally sampled based on distance and area
        uint emitterCount = 0, emitterStride = 0;
        params.GetDimensions(emitterCount, emitterStride);
        let sampled   = params[uint32_t(emitterCount * sample1)];
        let selectPdf = 1. / float(emitterCount); // apply after

        EmitterSample ret = EmitterSample();

        switch (sampled.type)
        {
        case EmitterType::Point:
            ret.pdf      = 1.0;
            ret.distance = distance(si.pos, sampled.pos);
            ret.emissive = sampled.emissive;
            ret.to       = (sampled.pos - si.pos) / ret.distance; // normalize
            ret.normal     = -ret.to;
            ret.isInfinite = false;
            break;

        case EmitterType::Area:
            // TODO: more effective sampling
            let faceIndex = uint(sample3 * sampled.faceNum);
            let meshIndex = sampled.meshIndex;

            let index = uint3(indices[meshIndex][faceIndex * 3 + 0], indices[meshIndex][faceIndex * 3 + 1], indices[meshIndex][faceIndex * 3 + 2]);
            let face  = Face<V, I>(vertices[meshIndex][index.x], vertices[meshIndex][index.y], vertices[meshIndex][index.z], instances[meshIndex]);

            let v = face.sample(sample2);

            ret.pdf = 1.0 / (sampled.faceNum * face.area);
            ret.distance = distance(si.pos, v.pos);
            ret.emissive = sampled.emissive;
            ret.to       = (v.pos - si.pos) / ret.distance; // normalize
            ret.normal     = v.normal;
            ret.isInfinite = false;
            break;

        case EmitterType::Infinite:

            // TODO: uniform 
            ret.pdf      = k::inv4Pi;
            ret.distance = k::infty;
            ret.to       = si.frame.toWorld(Warp::toUniformSphere(sample2));
            ret.normal     = -ret.to;
            ret.isInfinite = true;

            if (sampled.texIndex == -1) // constant emissive
            {
                ret.emissive = sampled.emissive;
            }
            else // envmap 
            {
                // sample the direction of ret.to
                let phi   = k::inv2Pi * sign(ret.to.z) * acos(ret.to.x / sqrt(ret.to.x * ret.to.x + ret.to.z * ret.to.z));
                let theta = k::invPi * acos(ret.to.y / length(ret.to));
                let angle = float2(phi, theta);

                ret.emissive = textures[sampled.texIndex].Sample(texSampler, angle).xyz;
            }

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

    public static float pdf(const SurfaceInteraction si, const Optional<SurfaceInteraction> bsdfSi, const uint32_t emitterNum)
    {
        if (emitterNum == 0)
        {
            return 0.;
        }

        if (let bs = bsdfSi)
        {
            let to     = bs.pos - si.pos;
            let distSq = dot(to, to);

            let lightCos = abs(dot(-normalize(to), bs.normal));

            let invJacobian = distSq / lightCos; // from point sampling to directional sampling
            return invJacobian / bs.area / emitterNum;
        }

        // infinite emitter
        // TODO: uniform
        return k::inv4Pi / emitterNum;
    }
}