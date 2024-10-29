module Frame;

public struct Frame 
{
    public __init(float3 n_)
    {
        n = n_;

        if(n.z < 0.)
        {
            let c1 = 1.0 / (1.0 - n.z);
            let c2 = n.x * n.y * c1;
            t = float3(1.0 - n.x * n.x * c1, -c2, n.x);
            b = float3(c2, n.y * n.y * c1 - 1.0, -n.y);
        }
        else
        {
            let c1 = 1.0 / (1.0 + n.z);
            let c2 = -n.x * n.y * c1;
            t = float3(1.0 - n.x * n.x * c1, c2, -n.x);
            b = float3(c2, 1.0 - n.y * n.y * c1, -n.y);
        }
    }

    public __init(float3 n_, float3 t_, float3 b_)
    {
        n = n_;
        t = t_;
        b = b_;
    }

    public float3 toWorld(float3 v)
    {
        return v.x * t + v.y * b + v.z * n;
    }

    public float3 toLocal(float3 v)
    {
        return float3(dot(v, t), dot(v, b), dot(v, n));
    }

    // local axis x, y, z
    public float3 t, b, n;
}