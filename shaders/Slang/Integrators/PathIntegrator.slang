
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
    float3 normal;
    float2 uv;
    float4 joint;
    float4 weight;
}

Vertex barycentric(Vertex v1, Vertex v2, Vertex v3, float2 barycentric)
{
    let w = float3(1.f - barycentric.x - barycentric.y, barycentric.x, barycentric.y);

    Vertex rtn;
    rtn.pos = v1.pos * w.x + v2.pos * w.y + v3.pos * w.z;
    rtn.normal = normalize(v1.normal * w.x + v2.normal * w.y + v3.normal * w.z);
    rtn.uv = v1.uv * w.x + v2.uv * w.y + v3.uv * w.z;
    rtn.joint = v1.joint * w.x + v2.joint * w.y + v3.joint * w.z;
    rtn.weight = v1.weight * w.x + v2.weight * w.y + v3.weight * w.z;

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

    // float frameWidth = sceneParams.screenSize.x / sceneParams.screenSize.y * sceneParams.frameHeight;
    // float imageY = (threadIdx.y / sceneParams.screenSize.y - 0.5f) * sceneParams.frameHeight;
    // float imageX = (threadIdx.x / sceneParams.screenSize.x - 0.5f) * frameWidth;
    // float imageZ = sceneParams.focalLength;
    // float3 rayDir = normalize(sceneParams.cameraDir.xyz*imageZ - uniforms.cameraUp.xyz * imageY + uniforms.cameraRight.xyz * imageX);

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
    // DEBUG
    // let dbginstanceIndex = InstanceIndex();
    // let dbgprimitiveIndex = PrimitiveIndex();
    // //let dbgindices = int3(indices[dbginstanceIndex][dbgprimitiveIndex * 3 + 0], indices[dbginstanceIndex][dbgprimitiveIndex * 3 + 1], indices[dbginstanceIndex][dbgprimitiveIndex * 3 + 2]);
    // let dbgVertex = vertices[dbginstanceIndex][0];
    // payload.color = float4(normalize(float3(dbgVertex.normal)), 1.0);
    // return;

    float3 hitLocation = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();
    float3 shadowRayDir = sceneParams.lightDir.xyz;

    // RayDesc ray;
    // ray.Origin = hitLocation;
    // ray.Direction = shadowRayDir;
    // ray.TMin = 0.001;
    // ray.TMax = 10000.0;
    // RayPayload shadowPayload = { float4(1.0, 0, 0, 0) };
    // TraceRay(sceneBVH, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH, ~0, 0, 0, 1, ray, shadowPayload);
    // float shadow = 1.0 - shadowPayload.color.x;

    let instanceIndex = InstanceIndex();
    let primitiveIndex = PrimitiveIndex();
    let indices = uint3(indices[instanceIndex][primitiveIndex * 3 + 0], indices[instanceIndex][primitiveIndex * 3 + 1], indices[instanceIndex][primitiveIndex * 3 + 2]);
    let vertex = barycentric(vertices[instanceIndex][indices.x], vertices[instanceIndex][indices.y], vertices[instanceIndex][indices.z], attr.barycentrics);
    payload.color = float4(vertex.normal, 1.0f);

    // float3 color = float3(0.8, 0.2, 0.2);
    // if (instanceIndex % 3 == 1)
    // {
    //     color = float3(0.2, 0.8, 0.2);
    // }
    // else if (instanceIndex % 3 == 2)
    // {
    //     color = float3(0.2, 0.2, 0.8);
    // }

    // float ndotl = max(0.0, shadow * dot(normal, sceneParams.lightDir.xyz));
    // float intensity = ndotl * 0.7 + 0.3;
    // payload.color = float4(color * intensity, 1.0f);
}

[shader("miss")]
void shadowMissShader(inout RayPayload payload : SV_RayPayload)
{
    payload.color = float4(0.0, 0.0, 0.0, 0.0);
}