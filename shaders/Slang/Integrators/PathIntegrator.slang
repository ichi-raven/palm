

import "../Sampler/Sampler";
import "../Material/Material";
import "../Emitter/Emitter";
import "../Utility/SurfaceInteraction";
import "../Utility/Frame";
import "../Utility/Constants";

struct SceneParams
{
    float4x4 view;
    float4x4 proj;
    float4x4 viewInv;
    float4x4 projInv;
    float4 cameraPos;

    uint32_t sppPerFrame;
    uint32_t areaEmitterNum;
    float2 padding;
}

struct InstanceParams : IInstance
{
    float4x4 world;
    float4x4 worldInvTrans;
}

struct Payload //<Sampler: ISampler> TODO:
{
    __init(const IndependentSampler sampler_, const bool sampleOnlyEmissive_ = true)
    {
        ctx = BSDFContext();
        sampler = sampler_;
        sampleOnlyEmissive = sampleOnlyEmissive_;
        resetForNextBounce();
    }

    BSDFContext ctx;
    Optional<SurfaceInteraction> si;
    Optional<BSDFSample> bsdfSample;
    Optional<EmitterSample> emitterSample;
    Optional<float3> emissive;
    IndependentSampler sampler;

    bool sampleOnlyEmissive;

    bool skipSampling()
    {
        return !sampleOnlyEmissive;
    }

    bool continue()
    {
        return si.hasValue && bsdfSample.hasValue && emitterSample.hasValue && !emissive.hasValue;
    }

    [mutating]
    void resetForNextBounce()
    {
        si              = none;
        bsdfSample      = none;
        emitterSample   = none;
        emissive        = none;
    }
}

struct Vertex : IVertex// std140
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
        let w = float3(1. - (barycentric.x + barycentric.y), barycentric.x, barycentric.y);

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
    let d         = (screenPos + offset) * 2.0 - 1.0;
    let target    = mul(sceneParams.projInv, float4(d.x, d.y, 1, 1));
    let direction = mul(sceneParams.viewInv, float4(target.xyz, 0)).xyz;

    RayDesc ray;
    ray.Origin = sceneParams.cameraPos.xyz;
    ray.Direction = normalize(direction);
    ray.TMin = k::eps;
    ray.TMax = k::infty;
    return ray;
}

bool occluded(const float3 pos, const EmitterSample es)
{
    // trace shadow ray
    RayDesc shadowRay;
    shadowRay.Origin               = pos;
    shadowRay.Direction            = normalize(es.to);
    shadowRay.TMin                 = k::eps;  // WARN: adhoc
    shadowRay.TMax                 = es.distance - k::eps;

    bool ret = true;// payload, true if hit other object
    TraceRay(sceneBVH, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER, ~0, 0, 0, 1, shadowRay, ret);

    return ret;
}

float3 sampleL<let enableMIS : bool>(in int sampleID, in int pixelSeed)
{
    float3 L    = float3(0.0);
    float3 beta = float3(1.0);

    let seed = tea(sampleID, pixelSeed);
    Payload payload    = Payload(IndependentSampler(seed));
    
    // trace primary ray
    RayDesc ray = getCameraRay(DispatchRaysIndex().xy, payload.sampler.next2D());
    TraceRay(sceneBVH, RAY_FLAG_NONE, ~0, 0, 0, 0, ray, payload);

    // WARNING: required for MIS
    if (enableMIS)
    {
        if (let emissive = payload.emissive)
        {
            L += beta * emissive;
        }
    }

    // trace ray recursively
    for (int depth = 1; depth < k::maxDepth && payload.continue(); ++depth)
    {
        {// russian roulette
            let prr = max(max(beta.x, beta.y), beta.z);
            if (payload.sampler.next1D() >= prr)
            {
                break;
            }
            beta /= prr;
        }
        
        let si = payload.si.value;
        let bs = payload.bsdfSample.value;
        let es = payload.emitterSample.value;

        if (enableMIS)
        {
            // NEE
            if (!bs.isSpecular()) 
            {
                if (!occluded(si.pos, es)) // HACK: Slang (HLSL) does not short circuit
                {
                    MaterialParams params = materialParams[si.instanceIndex];

                    // Add emitter directional contribution to L
                    let wo = si.frame.toLocal(normalize(es.to));
                    let lightCos = abs(dot(normalize(es.to), es.normal));
                    let G = lightCos / (es.distance * es.distance);
                    let cosine = abs(dot(si.normal, normalize(es.to)));
                    let f = DynamicMaterial.BSDF.eval(params, payload.ctx, si.toLocal(), wo);

                    let pathpdf   = G * DynamicMaterial.BSDF.pdf(params, payload.ctx, si.toLocal(), wo);
                    let MISWeight = es.pdf / es.pdf + pathpdf;
                    
                    L += MISWeight * beta * f * G * cosine * es.emissive / es.pdf;
                }
            }

            // BSDF sampling
            {
                Payload bsdfPayload = Payload(payload.sampler, false);

                RayDesc bsdfRay;
                bsdfRay.Origin      = si.pos;
                bsdfRay.Direction   = si.frame.toWorld(bs.wo);
                bsdfRay.TMin        = k::eps;
                bsdfRay.TMax        = k::infty;
                TraceRay(sceneBVH, RAY_FLAG_NONE, ~0, 0, 0, 0, bsdfRay, bsdfPayload);

                if (let emissive = bsdfPayload.emissive)
                {
                    // add BSDF directional contribution to L
                    float emitterpdf = 0.0;

                    if (let bsdfSi = bsdfPayload.si)
                    {
                        if (!bs.isSpecular())
                        {
                            uint emitterCount, emitterStride;
                            emitterParams.GetDimensions(emitterCount, emitterStride);
                            emitterpdf = EmitterSampler.pdf(si, bsdfSi, emitterCount);
                        }
                    }

                    let cosine    = abs(dot(si.normal, si.frame.toWorld(bs.wo)));
                    let MISWeight = bs.pdf / max(k::eps, emitterpdf + bs.pdf);
                    L += MISWeight * beta * bs.f * cosine * emissive / bs.pdf;
                }

                // reflects sampler transitions
                payload.sampler = bsdfPayload.sampler;
            }
        }

        // update parameters
        beta *= bs.f * abs(dot(si.normal, si.frame.toWorld(bs.wo))) / bs.pdf;

        ray.Origin      = si.pos;
        ray.Direction   = si.frame.toWorld(bs.wo);
        ray.TMin        = k::eps;
        ray.TMax        = k::infty;
        payload.resetForNextBounce();

        // trace next ray
        TraceRay(sceneBVH, RAY_FLAG_NONE, ~0, 0, 0, 0, ray, payload);
    }

    // WARNING: to be removed when doing MIS
    if (!enableMIS)
    {
        if (let e = payload.emissive)
        {
            L += beta * e;
        }
    }

    return L;
}

[ForceInline]
float3 correct(const float3 L)
{
    let invalid = any(isnan(L)) || any(isinf(L));
    return invalid ? k::zeros.xyz : L;
}

// bindings
[[vk::binding(0, 0)]] RaytracingAccelerationStructure sceneBVH;
[[vk::binding(1, 0)]] RWTexture2D resultImage;
[[vk::binding(2, 0)]] RWTexture2D poolImage;
[[vk::binding(3, 0)]] ConstantBuffer<SceneParams> sceneParams;
[[vk::binding(4, 0)]] StructuredBuffer<Vertex> vertices[];
[[vk::binding(5, 0)]] StructuredBuffer<uint32_t> indices[];
[[vk::binding(6, 0)]] StructuredBuffer<InstanceParams> instanceParams;
[[vk::binding(7, 0)]] StructuredBuffer<MaterialParams> materialParams;
[[vk::binding(8, 0)]] StructuredBuffer<EmitterParams> emitterParams;

[shader("raygeneration")]
void rayGenShader()
{
    uint2 threadIdx = DispatchRaysIndex().xy;
    if (threadIdx.x >= DispatchRaysDimensions().x) return;
    if (threadIdx.y >= DispatchRaysDimensions().y) return;

    let pool = poolImage[threadIdx.xy].xyz;
    let accumulatedSpp = reinterpret<uint>(poolImage[threadIdx.xy].w) + sceneParams.sppPerFrame;

    let pixelSeed = tea(accumulatedSpp, tea(threadIdx.x, threadIdx.y));

    float3 L = float3(0.);

    for (int sampleID = 0; sampleID < sceneParams.sppPerFrame; ++sampleID)
    {
        // true if doing MIS
        L += correct(sampleL<true>(sampleID, pixelSeed)) / float(sceneParams.sppPerFrame);
    }

    let rate = float(sceneParams.sppPerFrame) / accumulatedSpp;

    let finalRes = lerp(pool, L, rate);
    poolImage[threadIdx.xy] = float4(finalRes, reinterpret<float>(accumulatedSpp));

    let gamma = 2.2;  // HACK: gamma correction
    float3 gammaCorrected = pow(finalRes, 1. / gamma); 
    
    resultImage[threadIdx.xy] = float4(gammaCorrected, 1.0);
}

[shader("miss")]
void missShader(inout Payload payload : SV_RayPayload)
{
    payload.si = none;
    payload.bsdfSample = none;
    // TODO: infinite emitter
    payload.emitterSample = none;
    payload.emissive = none;
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

    let worldPos = mul(instanceParams[instanceIndex].world, float4(vertex.pos, 1.0)).xyz;
    let worldNormal = normalize(mul(instanceParams[instanceIndex].worldInvTrans, float4(vertex.normal, 0.)).xyz);

    // create SurfaceInteraction (world)
    let area = area(vertices[instanceIndex][index.x].pos, vertices[instanceIndex][index.y].pos, vertices[instanceIndex][index.z].pos);
    payload.si = SurfaceInteraction(worldPos, -worldRayDir, worldNormal, area, instanceIndex, Frame(worldNormal));
    
    // sample BSDF and emitter
    MaterialParams params = materialParams[instanceIndex];
    if (dot(params.emissive, params.emissive) > 0.)// HACK:
    {
        payload.emissive = params.emissive;
    }

    if (payload.skipSampling())
    {
        return;
    }

    payload.bsdfSample = DynamicMaterial.BSDF.sample(params, payload.ctx, payload.si.value.toLocal(), payload.sampler.next1D(), payload.sampler.next2D());
    if (let bs = payload.bsdfSample) // update BSDFContext
    {
        payload.ctx.update(bs, params.IOR);
        if (bs.pdf == 0.)
        {
            payload.bsdfSample = none;
        }
    }

    // emitter sample
    payload.emitterSample = EmitterSampler.sample<Vertex, InstanceParams>(emitterParams, vertices, indices, instanceParams, payload.si.value, payload.sampler.next1D(), payload.sampler.next2D(), payload.sampler.next1D());
}

[shader("miss")]
void shadowMissShader(inout bool occluded : SV_RayPayload)
{
    occluded = false;
}