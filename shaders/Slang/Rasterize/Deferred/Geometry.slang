import "../../Material/Material.slang";

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
    uint32_t entitySlot;
    float3 padding;
    uint32_t entityIndex;
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
        set {u = newValue.x; v = newValue.y; }
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
    float4 albedo               : SV_Target0;
    float4 worldPos             : SV_Target1;
    float4 normal               : SV_Target2;
    float4 roughnessMetalness   : SV_Target3;
};

[[vk::binding(0, 0)]] ConstantBuffer<SceneParams>  sceneParams;

[[vk::binding(0, 1)]] ConstantBuffer<EntityParams> entityParams;

[[vk::binding(0, 2)]] ConstantBuffer<MaterialParams>    materialParams;
// [[vk::binding(1, 2)]] Texture2D<float4>                 textures[];
// [[vk::binding(2, 2)]] SamplerState                      sampler;

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
    FSOutput output = (FSOutput)0;
    output.albedo               = float4(materialParams.albedo, 1.0);
    output.normal               = float4(in.worldInfo.normal, reinterpret<float>(entityParams.entitySlot));
    output.worldPos             = float4(in.worldInfo.pos, reinterpret<float>(entityParams.entityIndex));
    output.roughnessMetalness   = float4(materialParams.roughness, materialParams.metallic, 1.0, 1.0);

    return output;
}