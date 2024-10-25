#include "ISampler.slang"

public struct IndependentSampler : ISampler
{
    __init(int seed)
    {
        mRngState = seed;
    }

    [mutating]
    public float next1D()
    {
        mRngState = mRngState * 747796405 + 1;
        uint word = ((mRngState >> ((mRngState >> 28) + 4)) ^ mRngState) * 277803737;
        word      = (word >> 22) ^ word;
        return float(word) / 4294967295.0f;
    }

    [mutating]
    public float2 next2D()
    {
        return float2(next1D(), next1D());
    }

    internal int mRngState;
}