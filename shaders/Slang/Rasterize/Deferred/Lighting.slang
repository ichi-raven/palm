import "../../Utility/Constants";
import "../../Utility/SurfaceInteraction";
import "../../Utility/Frame";

import "../../Material/Material.slang";

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
[[vk::binding(3, 0)]] Texture2D<float4> roughnessMetalnessTex;
[[vk::binding(4, 0)]] SamplerState sampler;

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
    let albedo              = albedoTex.Sample(sampler, in.uv);
    let worldPos            = worldPosTex.Sample(sampler, in.uv); 
    let normal              = normalTex.Sample(sampler, in.uv); 
    let roughnessMetalness  = roughnessMetalnessTex.Sample(sampler, in.uv); 

    if (albedo.w == 0.)
    {
        return float4(k::white * 0.2, 0.0);
    }

    let slot    = reinterpret<uint32_t>(normal.w);
    let index   = reinterpret<uint32_t>(worldPos.w);
    let entity  = uint64_t(uint64_t(slot) << 32 | index);

    let uvYinv          = float2(saturate(in.uv.x), saturate(1.0 - in.uv.y));
    let mouseInFrame    = uint2(sceneParams.mousePos * sceneParams.frameSize);
    let uvInFrame       = uint2(uvYinv * sceneParams.frameSize);

    if (all(mouseInFrame == uvInFrame))
    {
        pickedID[0] = entity;
    }

    MaterialParams params = (MaterialParams)0;
    params.type = MaterialType::Principle;
    params.albedo = albedo.xyz;
    params.roughness = roughnessMetalness.x;
    params.metallic = roughnessMetalness.y;

    // there are no context
    var dummyCtx = BSDFContext();

    let invView = (sceneParams.camPos - worldPos).xyz;
    let frame = Frame(normal.xyz);
    let si = SurfaceInteraction(worldPos.xyz, invView, normal.xyz, float2(0.0, 0.0), 0.0, index, frame);

    let dummywo = frame.toLocal(normalize(float3(1., 1., 1.)));
    let dummyAmbient = float3(0.2, 0.2, 0.2);
    let dummyEmissive = k::white * 3.0;

    let finalColor = dummyAmbient + dummyEmissive * saturate(DynamicMaterial.BSDF.eval(params, dummyCtx, si, dummywo));

    return float4(finalColor, 1.0);
}