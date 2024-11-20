
implementing Sampler;

public struct IndependentSampler// : ISampler
{
    public __init(uint seed) 
    {
        prngState = seed;
    }

    [mutating]
    public float next1D()
    {
        prngState = prngState * 747796405 + 1;
        uint word = ((prngState >> ((prngState >> 28) + 4)) ^ prngState) * 277803737;
        word      = (word >> 22) ^ word;
        return float(word) / 4294967295.0f;// * 0.999f;
    }

    [mutating]
    public float2 next2D()
    {
        return float2(next1D(), next1D());
    }

    private int prngState;
}