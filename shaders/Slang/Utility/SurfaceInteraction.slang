module SurfaceInteraction;

import "Frame";

public struct SurfaceInteraction
{
    public __init(const float3 pos_, const float3 wi_, const float3 normal_, const Frame frame_)
    {
        pos     = pos_;
        wi      = normalize(wi_);
        normal  = normalize(normal_);
        frame   = frame_;
    }
    
    public SurfaceInteraction toLocal()
    {
        return SurfaceInteraction(pos, normalize(frame.toLocal(wi)), normalize(frame.toLocal(normal)), frame);
    }

    public SurfaceInteraction toWorld()
    {
        return SurfaceInteraction(pos, normalize(frame.toWorld(wi)), normalize(frame.toWorld(normal)), frame);
    }

    public float3 pos;
    public float3 wi;
    public float3 normal;

    public Frame frame;
}