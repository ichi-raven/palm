
struct BSDFSample
{
    [Flags]
    enum Flags
    {
        Reflection,    //  = 1
        Transmission,  //  = 2
        Diffuse,       //  = 4
        Glossy,        //  = 8
        Specular,      //  = 16
    }

    float3 f;
    float3 wi;
    float pdf;
    Flags flags;
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