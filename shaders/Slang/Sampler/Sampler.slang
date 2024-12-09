
module Sampler;

__include "IndependentSampler";

public interface ISampler
{
    public __init(uint32_t seed);

    [mutating]
    public float next1D();
    
    [mutating]
    public float2 next2D();
}

public uint32_t tea(uint32_t v0, uint32_t v1)
{
    uint32_t s0 = 0;

    [unroll]
    for (uint32_t n = 0; n < 16; ++n)
    {
        s0 += 0x9e3779b9;
        v0 += ((v1 << 4) + 0xa341316c) ^ (v1 + s0) ^ ((v1 >> 5) + 0xc8013ea4);
        v1 += ((v0 << 4) + 0xad90777d) ^ (v0 + s0) ^ ((v0 >> 5) + 0x7e95761e);
    }

    return v0;
}