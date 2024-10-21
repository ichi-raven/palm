struct VSOutput
{
    float4 SVPos : SV_Position;
    float2 uv : TexCoord;
};

[[vk::binding(0, 0)]] Texture2D<float4> albedoTex;
[[vk::binding(1, 0)]] Texture2D<float4> worldPosTex;
[[vk::binding(2, 0)]] Texture2D<float4> normalTex;
[[vk::binding(3, 0)]] SamplerState sampler;

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
    float4 output = normalTex.Sample(sampler, in.uv); // DEBUG
    return output;
}