implementing Material;

import "../Utility/Constants";

public struct Lambert : IMaterial
{
    public __init(in MaterialParams params_)
    {
        //mParams = params_;
    }

    public struct BSDF : IBSDF
    {
        public static Optional<BSDFSample> sample(MaterialParams params, BSDFContext ctx, SurfaceInteraction si, float sample1, float2 sample2)
        {
            BSDFSample ret;
            ret.flags = BSDFSample::Flags::Diffuse | BSDFSample::Flags::Reflection;

            ret.wo = Warp::toCosineWeightedHemisphere(sample2);
            if (ret.wo.z * si.wi.z <= 0.)  // not in same hemisphere
            {
                return none;
            }

            ret.f = eval(params, ctx, si, ret.wo);

            ret.pdf = pdf(params, ctx, si, ret.wo);

            return ret;
        }

        public static float pdf(MaterialParams params, BSDFContext ctx, SurfaceInteraction si, float3 wo)
        {
            if (wo.z * si.wi.z <= 0.)  // not in same hemisphere
            {
                return 0.;
            }

            return k::invPi * abs(wo.z);
        }

        public static float3 eval(MaterialParams params, BSDFContext ctx, SurfaceInteraction si, float3 wo)
        {
            if (wo.z * si.wi.z <= 0.)  // not in same hemisphere
            {
                return float3(0.);
            }

            return params.albedo.xyz * k::invPi;
        }
    }

    //private MaterialParams mParams = {};
}