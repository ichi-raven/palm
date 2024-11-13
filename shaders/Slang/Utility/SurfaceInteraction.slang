module SurfaceInteraction;

import "Frame";

public struct SurfaceInteraction
{
    public __init(const float3 pos_, const float3 wi_, const float3 normal_, const uint instanceIndex_, const Frame frame_)
    {
        pos     = pos_;
        wi      = normalize(wi_);
        normal  = normalize(normal_);
        instanceIndex = instanceIndex_;
        frame   = frame_;
    }

    public SurfaceInteraction toLocal()
    {
        return SurfaceInteraction(pos, normalize(frame.toLocal(wi)), normalize(frame.toLocal(normal)), instanceIndex, frame);
    }

    public SurfaceInteraction toWorld()
    {
        return SurfaceInteraction(pos, normalize(frame.toWorld(wi)), normalize(frame.toWorld(normal)), instanceIndex, frame);
    }

    public float3 pos;
    public float3 wi;
    public float3 normal;
    public uint instanceIndex;
    public Frame frame;
}