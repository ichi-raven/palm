implementing Material;

public struct Lambert : IMaterial
{

    public __init(in MaterialParams params_)
    {
        params = params_;
    }



    public struct BSDF : IBSDF
    {
        public BSDFSample sample()
        {

        }

        public float pdf()
        {

        }

        public float3 eval()
        {

        }
    }

    public MaterialParams params;
}