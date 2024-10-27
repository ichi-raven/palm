module Emitter;

public struct EmitterSample
{
    public float pdf;
    public float3 emissive;
    public float3 to;
    public float distance;
}

interface IEmitter
{
    __init();

    
}