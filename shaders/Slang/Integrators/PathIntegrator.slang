
struct SceneParams
{
    float4x4 view;
    float4x4 proj;
    float4x4 viewInv;
    float4x4 projInv;
    float4 cameraPos;
    float2 screenSize;
    float2 padding;
    float4 lightDir;
};

struct RayPayload
{
    float4 color;
};

struct Vertex
{
    float3 pos;
    float u;
    float3 normal;
    float v;
}

Vertex barycentric(Vertex v1, Vertex v2, Vertex v3, float2 barycentric)
{
    let w = float3(1.f - barycentric.x - barycentric.y, barycentric.x, barycentric.y);

    Vertex rtn;
    rtn.pos = v1.pos * w.x + v2.pos * w.y + v3.pos * w.z;
    rtn.normal = v1.normal * w.x + v2.normal * w.y + v3.normal * w.z;
    rtn.u = v1.u * w.x + v2.u * w.y + v3.u * w.z;
    rtn.v = v1.v * w.x + v2.v * w.y + v3.v * w.z;

    return rtn;
}

RayDesc getCameraRay(uint2 threadIdx)
{
    let pixelCenter = float2(threadIdx.xy) + float2(0.5);
    let screenPos = pixelCenter / sceneParams.screenSize.xy;
    
    //const float2 offset = float2(stepAndOutputRNGFloat(prngState), stepAndOutputRNGFloat(prngState)) / sceneParams.screenSize.xy;
    
    //const vec2 d = (screenPos + offset) * 2.0 - 1.0;
    let d = screenPos * 2.0 - 1.0;
    let target = mul(float4(d.x, d.y, 1, 1), sceneParams.projInv);
    let direction = mul(float4(target.xyz, 0), sceneParams.viewInv).xyz;

    RayDesc ray;
    ray.Origin = sceneParams.cameraPos.xyz;
    ray.Direction = normalize(direction);
    ray.TMin = 0.001;
    ray.TMax = 10000.0;
    return ray;
}

[[vk::binding(0, 0)]] RaytracingAccelerationStructure sceneBVH;
[[vk::binding(1, 0)]] RWTexture2D resultTexture;
[[vk::binding(2, 0)]] ConstantBuffer<SceneParams> sceneParams;
[[vk::binding(3, 0)]] StructuredBuffer<Vertex> vertices[];
[[vk::binding(4, 0)]] StructuredBuffer<uint32_t> indices[];

[shader("raygeneration")]
void rayGenShader()
{
    uint2 threadIdx = DispatchRaysIndex().xy;
    if (threadIdx.x >= (int)sceneParams.screenSize.x) return;
    if (threadIdx.y >= (int)sceneParams.screenSize.y) return;

    // Trace the ray.
    RayDesc ray = getCameraRay(threadIdx);
    RayPayload payload = { float4(0, 0, 0, 0) };
    TraceRay(sceneBVH, RAY_FLAG_NONE, ~0, 0, 0, 0, ray, payload);

    let rel = threadIdx / sceneParams.screenSize;
    let pixelCenter = float2(rel.xy) + float2(0.5);
    resultTexture[threadIdx.xy] = payload.color;
}

[shader("miss")]
void missShader(inout RayPayload payload : SV_RayPayload)
{
    payload.color = float4(0, 0, 0, 1);
}

[shader("closesthit")]
void closestHitShader(inout RayPayload payload : SV_RayPayload, 
    in BuiltInTriangleIntersectionAttributes attr)
{
    float3 hitLocation = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();
    float3 shadowRayDir = sceneParams.lightDir.xyz;

    let instanceIndex = InstanceIndex();
    let primitiveIndex = PrimitiveIndex();
    let index = uint3(indices[instanceIndex][primitiveIndex * 3 + 0], indices[instanceIndex][primitiveIndex * 3 + 1], indices[instanceIndex][primitiveIndex * 3 + 2]);
    let vertex = barycentric(vertices[instanceIndex][index.x], vertices[instanceIndex][index.y], vertices[instanceIndex][index.z], attr.barycentrics);

    let worldPos    = mul(float4(vertex.pos, 1.0), ObjectToWorld4x3());
    let worldNormal = mul(float4(vertex.normal, 0.0), ObjectToWorld4x3());

    RayDesc ray;
    ray.Origin               = worldPos;//hitLocation;
    ray.Direction            = shadowRayDir;
    ray.TMin                 = 0.001;
    ray.TMax                 = 10000.0;
    RayPayload shadowPayload = { float4(1.0, 0, 0, 0) };
    TraceRay(sceneBVH, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER, ~0, 0, 0, 1, ray, shadowPayload);
    let shadow = 1.0 - shadowPayload.color.x;

    float ndotl     = max(0.0, shadow * dot(vertex.normal, sceneParams.lightDir.xyz));
    float intensity = ndotl * 0.7 + 0.3;
    float3 color = float3(0.2, 0.8, 0.2);
    payload.color = float4(color * intensity, 1.0f);
}

[shader("miss")]
void shadowMissShader(inout RayPayload payload : SV_RayPayload)
{
    payload.color = float4(0.2, 0.2, 0.2, 0.0);
}