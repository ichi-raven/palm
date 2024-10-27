module SurfaceInteraction;

import "../Material/Material";

public struct SurfaceInteraction
{
    public __init(const float3 pos_, const float3 wi_, const float3 normal_)
    {
        pos = pos_;
        wi = wi_;
        normal = normal_;
    }

    public float3 pos;
    public float3 wi;
    public float3 normal;
}