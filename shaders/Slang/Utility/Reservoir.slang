module Reservoir;

import "../Sampler/Sampler";

public struct Reservoir<SampleType>
{
    public static Reservoir<SampleType> combine<S : ISampler, PDF : IFunc<float, SampleType>>(Reservoir<SampleType> rs[], PDF pdf, inout S sampler)
    {
        Reservoir<SampleType> s;

        [unroll]
        for (uint i = 0; i < rs.getCount(); ++i)
        {
            let weight = rs[i].weight() * float(rs[i].M) * pdf(rs[i].candidate);
            s.update(rs[i].candidate, weight, rs[i].p, sampler.next1D());
        }

        return s;
    }

    [mutating]
    public bool update(SampleType xi, const float wi, const float pi, const float u)
    {
        wSum += wi;
        ++streamLength;

        if (u < (wi / wSum))
        {
            x = xi;
            p = pi;
            return true;
        }

        return false;
    }

    public float weight()
    {
        return (1. / p) * (wSum / streamLength);
    }

    public property SampleType candidate
    {
        get { return x; }
    }

    public property uint M
    {
        get { return streamLength; }
    }

    SampleType x;
    float wSum;
    float p;
    uint streamLength;
    uint padding; // for alignment
}

// float streamingRIS(uint candidateNum, inout IndependentSampler sampler)
// {
//     Reservoir<float> r;

//     for (int i = 0; i < candidateNum; ++i)
//     {
//         let x      = invCdf(sampler.next1D());
//         let weight = targetPdf(x) / pdf(x);
//         let u      = sampler.next1D();

//         r.update(x, weight, targetPdf(x), u);
//     }

//     let fVal = f(r.getCandidate());
//     let w    = r.weight();

//     return fVal * w;
// }
