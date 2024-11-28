module Constants;

public namespace k
{
    public static let pi      = 3.14159265358979323846;
    public static let invPi   = 0.31830988618379067154;
    public static let inv2Pi  = 0.15915494309189533577;
    public static let inv4Pi  = 0.07957747154594766788;
    public static let piOver2 = 1.57079632679489661923;
    public static let piOver4 = 0.78539816339744830961;
    public static let sqrt2   = 1.41421356237309504880;

    public static let eps      = 1e-3;
    public static let infty    = 1e6;
    public static let maxDepth = 16;

    public static let invalidTexIndex = -1;

    public static let fixedEmitterLimit = 10;

    public static let zeros = float4(0.0);
    public static let ones  = float4(1.0);

    public static let red    = float3(0.8, 0.1, 0.1);
    public static let green  = float3(0.1, 0.8, 0.1);
    public static let blue   = float3(0.1, 0.1, 0.8);
    public static let yellow = float3(0.8, 0.8, 0.1);
    public static let black  = float3(0.0);
    public static let white  = float3(1.0);

}