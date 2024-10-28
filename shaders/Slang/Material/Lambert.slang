implementing Material;

public struct Lambert : IMaterial
{

    public __init(in MaterialParams params_)
    {
        mParams = params_;
    }

    public property MaterialParams params
    {
        get {return mParams;}
    }

    public struct BSDF : IBSDF
    {
        public BSDFSample sample(BSDFContext ctx, SurfaceInteraction si, float sample1, float2 sample2)
        {
            
        }

        public float pdf(BSDFContext ctx, SurfaceInteraction si, float3 wo)
        {

        }

        public float3 eval(BSDFContext ctx, SurfaceInteraction si, float3 wo)
        {

        }
    }

    private MaterialParams mParams;
}