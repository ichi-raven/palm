

import "../Sampler/Sampler";
import "../Material/Material";
import "../Emitter/Emitter";
import "../Utility/SurfaceInteraction";


struct SceneParams
{
    float4x4 view;
    float4x4 proj;
    float4x4 viewInv;
    float4x4 projInv;
    float4 cameraPos;
    float4 lightDir;
};

struct Payload //<Sampler: ISampler> TODO:
{
    Optional<SurfaceInteraction> si;
    Optional<BSDFSample> bsdfSample;
    Optional<EmitterSample> emitterSample;
    Optional<float3> emissive;
    IndependentSampler sampler;

    bool continue()
    {
        return si.hasValue && bsdfSample.hasValue && emitterSample.hasValue && emissive.hasValue;
    }

    [mutating]
    void resetForNextBounce()
    {
        si              = none;
        bsdfSample      = none;
        emitterSample   = none;
        emissive        = none;
    }
};

struct Vertex// std140
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

    static Vertex barycentric(Vertex v1, Vertex v2, Vertex v3, float2 barycentric)
    {
        let w = float3(1.f - barycentric.x - barycentric.y, barycentric.x, barycentric.y);

        Vertex ret;
        ret.pos = v1.pos * w.x + v2.pos * w.y + v3.pos * w.z;
        ret.normal = v1.normal * w.x + v2.normal * w.y + v3.normal * w.z;
        ret.uv = v1.uv * w.x + v2.uv * w.y + v3.uv * w.z;

        return ret;
    }
}

RayDesc getCameraRay(uint2 threadIdx, float2 u)
{
    let pixelCenter = float2(threadIdx.xy) + float2(0.5);
    let screenPos = pixelCenter / DispatchRaysDimensions().xy;
    
    const float2 offset = u / DispatchRaysDimensions().xy;
    
    // TODO: lens sampling
    //let d = (screenPos) * 2.0 - 1.0;
    let d = (screenPos + offset) * 2.0 - 1.0;
    let target = mul(float4(d.x, d.y, 1, 1), sceneParams.projInv);
    let direction = mul(float4(target.xyz, 0), sceneParams.viewInv).xyz;

    RayDesc ray;
    ray.Origin = sceneParams.cameraPos.xyz;
    ray.Direction = normalize(direction);
    ray.TMin = 0.001;
    ray.TMax = 10000.0;
    return ray;
}

bool occluded(const float3 pos, const EmitterSample es)
{
    // trace shadow ray
    RayDesc shadowRay;
    shadowRay.Origin               = pos;//hitLocation;
    shadowRay.Direction            = es.to;
    shadowRay.TMin                 = 0.001;// WARN: adhoc
    shadowRay.TMax                 = es.distance - 0.0001;// WARN: adhoc
    bool ret = false;// payload, 
    TraceRay(sceneBVH, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER, ~0, 0, 0, 1, shadowRay, ret);

    return ret;
}

float3 sampleL(in int sampleID, in int pixelSeed)
{
    float3 L    = float3(0.0);
    float3 beta = float3(1.0);

    Payload payload = {};
    payload.sampler = IndependentSampler(tea(sampleID, pixelSeed));
    
    // trace primary ray
    RayDesc ray = getCameraRay(DispatchRaysIndex().xy, payload.sampler.next2D());
    TraceRay(sceneBVH, RAY_FLAG_NONE, ~0, 0, 0, 0, ray, payload);

    if (let emissive = payload.emissive)
    {
        L += beta * emissive;
    }

    // trace ray recursively
    for (int depth = 0; depth < 8 && payload.continue(); depth++)
    {
        {// russian roulette
            const float prr = max(max(beta.x, beta.y), beta.z);
            if (payload.sampler.next1D() >= prr)
            {
                break;
            }
            beta /= prr;
        }
        
        let si = payload.si.value;
        let bs = payload.bsdfSample.value;
        let es = payload.emitterSample.value;

        // NEE
        if (!bs.isSpecular()) 
        {
            if (!occluded(si.pos, es)) // HACK: Slang (HLSL) does not short circuit
            {
                // Add emitter directional contribution to L

                // const float pathpdf = ls.G * pdfPBRTBSDF(payload.mat, -wo, normalize(ls.to), payload.normal);
                // const float cosine = abs(dot(payload.normal, normalize(ls.to)));
                // const vec3 f = evalPBRTBSDF(payload.mat, -wo, normalize(ls.to), payload.normal);
                // const float MISWeight = ls.pdf / (ls.pdf + pathpdf);
                // L += MISWeight * beta * f * ls.G * cosine * ls.L / ls.pdf;
            }
        }

        // BSDF sampling
        {
            Payload bsdfPayload;
            bsdfPayload.sampler = payload.sampler;
            TraceRay(sceneBVH, RAY_FLAG_NONE, ~0, 0, 0, 0, ray, bsdfPayload/**/);

            if (let emissive = bsdfPayload.emissive)
            {
                // Add BSDF directional contribution to L

            }

            // reflects sampler transitions
            payload.sampler = bsdfPayload.sampler;
        }

        // update parameters
        beta *= bs.f * abs(dot(si.normal, bs.wo)) / max(1e-4, bs.pdf); // HACK: avoid zero dividing
        payload.resetForNextBounce();
        ray.Origin = si.pos;
        ray.Direction = bs.wo;

        // trace next ray
        TraceRay(sceneBVH, RAY_FLAG_NONE, ~0, 0, 0, 0, ray, payload);
    }

    if (let e = payload.emissive)
    {
        L += beta * e;
    }

    return L;
}

// bindings
[[vk::binding(0, 0)]] RaytracingAccelerationStructure sceneBVH;
[[vk::binding(1, 0)]] RWTexture2D resultTexture;
[[vk::binding(2, 0)]] ConstantBuffer<SceneParams> sceneParams;
[[vk::binding(3, 0)]] StructuredBuffer<Vertex> vertices[];
[[vk::binding(4, 0)]] StructuredBuffer<uint32_t> indices[];

[shader("raygeneration")]
void rayGenShader()
{
    uint2 threadIdx = DispatchRaysIndex().xy;
    if (threadIdx.x >= DispatchRaysDimensions().x) return;
    if (threadIdx.y >= DispatchRaysDimensions().y) return;

    let spp = 1024; // TODO: change from scene parameter
    let pixelSeed = tea(threadIdx.x, threadIdx.y);

    float3 L = float3(0.);

    for (int sampleID = 0; sampleID < spp; ++sampleID)
    {
        L += sampleL(sampleID, pixelSeed) / float(spp);
    }

    float3 gammaCorrected = sqrt(L); // HACK:
    resultTexture[threadIdx.xy] = float4(gammaCorrected, 1.0);
}

[shader("miss")]
void missShader(inout Payload payload : SV_RayPayload)
{
    payload.si = none;
    payload.bsdfSample = none;
    // uniform emitter sample?
    payload.emitterSample = none;
    // lighting
    payload.emissive = float3(0.3, 0.3, 0.3);
}

[shader("closesthit")]
void closestHitShader(inout Payload payload : SV_RayPayload, 
    in BuiltInTriangleIntersectionAttributes attr)
{
    let worldRayDir = WorldRayDirection();
    let hitLocation = WorldRayOrigin() + worldRayDir * RayTCurrent();

    let instanceIndex = InstanceIndex();
    let primitiveIndex = PrimitiveIndex();
    let index = uint3(indices[instanceIndex][primitiveIndex * 3 + 0], indices[instanceIndex][primitiveIndex * 3 + 1], indices[instanceIndex][primitiveIndex * 3 + 2]);
    let vertex = Vertex.barycentric(vertices[instanceIndex][index.x], vertices[instanceIndex][index.y], vertices[instanceIndex][index.z], attr.barycentrics);

    let worldPos    = mul(float4(vertex.pos, 1.0), ObjectToWorld4x3());
    let worldNormal = mul(float4(vertex.normal, 0.0), ObjectToWorld4x3());

    // create SurfaceInteraction
    SurfaceInteraction si = SurfaceInteraction(worldPos, worldRayDir, worldNormal);
    // sample BSDF and emitter? or where?
    

}

[shader("miss")]
void shadowMissShader(inout bool occluded : SV_RayPayload)
{
    occluded = true;
}