
struct SceneParams
{
    float4x4 view;
    float4x4 proj;
    float4x4 viewInv;
    float4x4 projInv;
    float4 camPos;
    float2 mousePos;
    uint2 frameSize;
}

struct EntityParams
{
    float4x4 model;
    float4x4 modelInvTranspose;
    float3 vel;
    float padding1;
    uint64_t entityID;
    float2 padding2;
}

struct VSInput
{
    float3 pos;
    float u;
    float3 normal;
    float v;

    property float2 uv
    {
        get {return float2(u, v);}
        set {u = uv.x; v = uv.y; }
    }
};

struct WorldInfo
{
    float3 pos;
    float3 normal;
    float2 uv;
};

struct VSOutput
{
    WorldInfo worldInfo;
    float4 SVPos : SV_Position;
};

struct FSOutput
{
    float4 albedo   : SV_Target0;
    float4 worldPos : SV_Target1;
    float4 normal   : SV_Target2;
};

[[vk::binding(0, 0)]] ConstantBuffer<SceneParams>  sceneParams;
[[vk::binding(0, 1)]] ConstantBuffer<EntityParams> entityParams;

[shader("vertex")]
VSOutput vsmain(VSInput in)
{
    VSOutput output         = (VSOutput)0;

    output.worldInfo.pos    = mul(entityParams.model, float4(in.pos, 1.0)).xyz;
    output.worldInfo.normal = mul(entityParams.modelInvTranspose, float4(in.normal, 1.)).xyz;
    output.worldInfo.uv     = in.uv;

    output.SVPos = mul(sceneParams.proj, mul(sceneParams.view, float4(output.worldInfo.pos, 1.0)));

    return output;
}

[shader("fragment")]
FSOutput fsmain(VSOutput in)
{
    let slot  = uint32_t(entityParams.entityID & 0xFFFFFFFF << 32);
    let index = uint32_t(entityParams.entityID & 0xFFFFFFFF);

    FSOutput output = (FSOutput)0;
    output.albedo = float4(1.0); // TODO:
    output.normal = float4(in.worldInfo.normal, reinterpret<float>(slot));
    output.worldPos = float4(in.worldInfo.pos, reinterpret<float>(index));
    
    return output;
}