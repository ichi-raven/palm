

struct BSDFSample
{
    float3 f;
    float3 wi;
    float pdf;

}

interface IMaterial 
{
    associatedtype BSDF : IBSDF;

}

interface IBSDF
{
    BSDFSample sample();

    float pdf();

    float3 eval();
}