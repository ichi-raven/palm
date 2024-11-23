import "../../Utility/Constants";

struct VSOutput
{
    float4 SVPos : SV_Position;
    float2 uv : TexCoord;
};

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

[[vk::binding(0, 0)]] Texture2D<float4> albedoTex;
[[vk::binding(1, 0)]] Texture2D<float4> worldPosTex;
[[vk::binding(2, 0)]] Texture2D<float4> normalTex;
[[vk::binding(3, 0)]] SamplerState sampler;

[[vk::binding(0, 1)]] ConstantBuffer<SceneParams>  sceneParams;
[[vk::binding(1, 1)]] RWStructuredBuffer<uint64_t>  pickedID;

[shader("vertex")]
VSOutput vsmain(uint id : SV_VertexID)
{
    VSOutput output = (VSOutput)0;
    float x = float(id / 2);
    float y = float(id % 2);

    output.uv = float2(x, y);
    output.SVPos = float4(x * 2.f - 1.f, y * 2.f - 1.f, 0, 1.0);
    return output;
}

[shader("fragment")]
float4 fsmain(VSOutput in) : SV_Target
{
    float4 normal   = normalTex.Sample(sampler, in.uv); // DEBUG
    float4 worldPos = worldPosTex.Sample(sampler, in.uv); // DEBUG
    
    let slot  = reinterpret<uint32_t>(normal.w);
    let index = reinterpret<uint32_t>(worldPos.w);
    let entity = uint64_t(uint64_t(slot) << 32 | index);

    float2 uvYinv = float2(saturate(in.uv.x), saturate(1.0 - in.uv.y));
    uint2 mouseInFrame = uint2(sceneParams.mousePos * sceneParams.frameSize);
    uint2 uvInFrame = uint2(uvYinv * sceneParams.frameSize);

    if (all(mouseInFrame == uvInFrame))
    {
        pickedID[0] = entity;
    }

    return normal;// DEBUG
}