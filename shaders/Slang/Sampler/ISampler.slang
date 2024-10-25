
interface ISampler
{
    __init(int seed);

    [mutating]
    float next1D();
    
    [mutating]
    float2 next2D();
}