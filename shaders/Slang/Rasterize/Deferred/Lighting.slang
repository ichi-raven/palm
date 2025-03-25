import "../../Utility/Constants";
import "../../Utility/SurfaceInteraction";
import "../../Utility/Frame";

import "../../Material/Material";
import "../../Emitter/Emitter";

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
[[vk::binding(2, 1)]] ConstantBuffer<EmitterParams[10]>  emitterParams;
[[vk::binding(3, 1)]] Texture2D<float4> envmapTex;
[[vk::binding(4, 1)]] SamplerState envmapSampler;

[shader("vertex")]
VSOutput vsmain(uint id : SV_VertexID)
{
    VSOutput output = (VSOutput)0;
    let x = float(id / 2);
    let y = float(id % 2);

    output.uv = float2(x, y);
    output.SVPos = float4(x * 2.f - 1.f, y * 2.f - 1.f, 0, 1.0);

    return output;
}

float4 getPseudoFilteredEnvmap(const float alpha, const float2 uv, Texture2D<float4> texture, SamplerState texSampler)
{
    const float kernel          = 0.02 * alpha;
    const static int kernelSize = 2;
    const static int pixNum     = (kernelSize + 1) * (kernelSize + 1);

    var rtn = float4(0.0);

    [[unroll]]
    for (int i = -kernelSize; i <= kernelSize; ++i)
    {
        [[unroll]]
        for (int j = -kernelSize; j <= kernelSize; ++j)
        {
            let shifted = uv + float2(i * kernel, j * kernel);
            rtn += 1. / pixNum * texture.Sample(texSampler, shifted);
        }
    }

    return rtn;
}

inline float2 toPolar(const float3 direction)
{
    let phi   = k::inv2Pi * sign(direction.z) * acos(direction.x / sqrt(direction.x * direction.x + direction.z * direction.z));
    let theta = k::invPi * acos(direction.y / length(direction));

    return float2(phi, theta);
}

[shader("fragment")]
float4 fsmain(VSOutput in) : SV_Target
{
    let albedo              = albedoTex.Sample(sampler, in.uv);
    let worldPos            = worldPosTex.Sample(sampler, in.uv); 
    let normal              = normalTex.Sample(sampler, in.uv); 
    let roughnessMetalness  = roughnessMetalnessTex.Sample(sampler, in.uv);

    let uvYinv       = float2(saturate(in.uv.x), saturate(1.0 - in.uv.y));
    let mouseInFrame = uint2(sceneParams.mousePos * sceneParams.frameSize);
    let uvInFrame    = uint2(uvYinv * sceneParams.frameSize);

    static let gamma = 2.2;  // HACK: gamma correction

    if (albedo.w == 0.)
    {
        let screenPos = uvYinv;

        let d         = screenPos * 2.0 - 1.0;
        let target    = mul(sceneParams.projInv, float4(d.x, d.y, 1, 1));
        let direction = normalize(mul(sceneParams.viewInv, float4(target.xyz, 0)).xyz);

        let angle = toPolar(direction);

        return float4(pow(envmapTex.Sample(sampler, angle).xyz, 1. / gamma), 1.0);
    }

    let slot    = reinterpret<uint32_t>(normal.w);
    let index   = reinterpret<uint32_t>(worldPos.w);
    let entity  = uint64_t((uint64_t(slot) << 32) | index);

    if (all(mouseInFrame == uvInFrame))
    {
        pickedID[0] = entity;
    }

    var params = MaterialParams();
    params.type = MaterialType::Principle;
    params.albedo = albedo.xyz;
    params.roughness = roughnessMetalness.x;
    params.metallic = roughnessMetalness.y;

    // there is no context
    var dummyCtx = BSDFContext();
    dummyCtx.lastIOR = 1.0;

    let invView = normalize((sceneParams.camPos - worldPos).xyz);
    let frame = Frame(normal.xyz);
    let si = SurfaceInteraction(worldPos.xyz, invView, normal.xyz, float2(0.0, 0.0), 0.0, index, frame);

    let ambient = 0.01 * albedo.xyz;
    var finalColor = ambient;

    // limited direct sampling, treat all emitters as pseudo point emitter
    for (int i = 0; i < k::fixedEmitterLimit; ++i)
    {
        if (emitterParams[i].type == EmitterType::Infinite)
        {
            let to       = reflect(si.wi, normal.xyz);
            let wo       = frame.toLocal(to);
            let f        = DynamicMaterial.BSDF.eval(params, dummyCtx, si.toLocal(), wo);

            let uv       = toPolar(to) * float2(1, -1);
            let emissive = getPseudoFilteredEnvmap(params.roughness, uv, envmapTex, sampler).xyz;
            finalColor += emissive * f * abs(dot(si.normal, si.frame.toWorld(wo)));
            continue;
        }

        var to = emitterParams[i].pos - si.pos;
        let distance = length(to);
        to = 1. / distance * (to);

        let wo = frame.toLocal(to);
        let emissive = emitterParams[i].emissive;
        let f = DynamicMaterial.BSDF.eval(params, dummyCtx, si.toLocal(), wo);
        let lightCos = abs(dot(normalize(to), -to));
        let G = lightCos / (distance * distance);

        finalColor += emissive * f * G * abs(dot(si.normal, si.frame.toWorld(wo)));
    }

    // TODO: envmap lighting
    // (transfer DG-integrated texture and filter)


    float3 gammaCorrected = pow(finalColor, 1. / gamma);
    return float4(gammaCorrected, 1.0);
}