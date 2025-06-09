module Color;

import Constants;

[ForceInline]
public float3 gammaToLinear(const float3 color, const float gamma)
{
    return pow(color, gamma);
}

[ForceInline]
public float3 linearToGamma(const float3 color, const float gamma)
{
    return pow(color, 1. / gamma);
}

[ForceInline]
public float toGray(const float3 color)
{
    return dot(color, float3(0.2126, 0.7152, 0.0722));
}