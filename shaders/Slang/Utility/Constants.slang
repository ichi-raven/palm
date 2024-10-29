module Constants;

public namespace k
{
    public static let pi     = 3.14159265358979;
    public static let pi2    = pi * 2;
    public static let invPi  = 1. / pi;
    public static let invPi2 = 1. / pi / pi;

    public static let eps      = 1e-3;
    public static let infty    = 1e6;
    public static let maxDepth = 8;

    public static let invalidTexIndex = -1;

    public static let zeros = float4(0.0);
    public static let ones  = float4(1.0);

    public static let red    = float3(0.85, 0.1, 0.1);
    public static let green  = float3(0.1, 0.85, 0.1);
    public static let blue   = float3(0.1, 0.1, 0.85);
    public static let yellow = float3(0.85, 0.85, 0.1);
    public static let black  = float3(0.0);
    public static let white  = float3(1.0);

}