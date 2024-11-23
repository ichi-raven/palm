module SurfaceInteraction;

import "Frame";

public float area(const float3 p0, const float3 p1, const float3 p2)
{
    return 0.5 * length(cross(p2 - p0, p1 - p0));
}

public struct SurfaceInteraction
{
    public __init(const float3 pos_, const float3 wi_, const float3 normal_, const float2 uv_, const float area_, const uint instanceIndex_, const Frame frame_)
    {
        pos     = pos_;
        wi      = normalize(wi_);
        normal  = normalize(normal_);
        uv = uv_;
        area = area_;
        instanceIndex = instanceIndex_;
        frame   = frame_;
    }

    public SurfaceInteraction toLocal()
    {
        return SurfaceInteraction(pos, normalize(frame.toLocal(wi)), normalize(frame.toLocal(normal)), uv, area, instanceIndex, frame);
    }

    public SurfaceInteraction toWorld()
    {
        return SurfaceInteraction(pos, normalize(frame.toWorld(wi)), normalize(frame.toWorld(normal)), uv, area, instanceIndex, frame);
    }

    public float3 pos;
    public float3 wi;
    public float3 normal;
    public float2 uv;
    public float area;
    public uint instanceIndex;
    public Frame frame;
}