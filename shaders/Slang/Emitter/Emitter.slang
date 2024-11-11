module Emitter;

import "../Utility/Constants";


public struct EmitterSample
{
    public float pdf;
    public float3 emissive;
    public float3 to;
    public float distance;
}

public enum Type
{
    Point = 0,
    Area = 1,
    Infinite = 2,
} 

public struct EmitterParams
{
    float3 pos = k::zeros.xyz;
    int32_t type = Type::Point;

    uint32_t faceNum  = 0;
    int32_t meshIndex = -1;
    float2 padding = k::zeros.xy;

    float3 emissive = k::zeros.xyz;
    int32_t texIndex = -1;
}

interface IEmitter
{
    __init();

    
}