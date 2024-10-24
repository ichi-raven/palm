
interface ISampler
{
    void init(int seed);

    float next1D();

    float2 next2D();
}