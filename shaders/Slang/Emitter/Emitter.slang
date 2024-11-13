module Emitter;

import "../Utility/Constants";


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

interface IEmitterSampler
{
    __init();

}