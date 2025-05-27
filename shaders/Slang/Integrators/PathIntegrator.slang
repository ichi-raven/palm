

import "../Sampler/Sampler";
import "../Material/Material";
import "../Emitter/Emitter";
import "../Utility/SurfaceInteraction";
import "../Utility/Frame";
import "../Utility/Warp";
import "../Utility/Constants";

struct SceneParams
{
    float4x4 view;
    float4x4 proj;
    float4x4 viewInv;
    float4x4 projInv;
    float4 cameraPos;

    uint32_t sppPerFrame;
    uint32_t allEmitterNum;
    uint32_t2 padding;
}

struct InstanceParams : IInstance
{
    float4x4 world;
    float4x4 worldInvTrans;
}

struct Payload //<Sampler: ISampler> TODO: selecting sampler
{
    __init(const IndependentSampler sampler_, const bool sampleOnlyEmissive_ = false)
    {
        ctx = BSDFContext();
        sampler = sampler_;
        sampleOnlyEmissive = sampleOnlyEmissive_;
        resetForNextBounce();
    }

    Optional<SurfaceInteraction> si;
    Optional<BSDFSample> bsdfSample;
    Optional<EmitterSample> emitterSample;
    Optional<float3> emissive;
    
    BSDFContext ctx;
    IndependentSampler sampler;
    bool sampleOnlyEmissive;

    bool skipSampling()
    {
        return sampleOnlyEmissive;
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
        set {u = newValue.x; v = newValue.y; }
    }

    static Vertex barycentric(const Vertex v1, const Vertex v2, const Vertex v3, const float2 barycentric)
    {
        let w = float3(1. - barycentric.x - barycentric.y, barycentric.x, barycentric.y);

        Vertex ret;
        ret.pos = v1.pos * w.x + v2.pos * w.y + v3.pos * w.z;
        ret.normal = v1.normal * w.x + v2.normal * w.y + v3.normal * w.z;
        ret.uv = v1.uv * w.x + v2.uv * w.y + v3.uv * w.z;

        return ret;
    }
}

RayDesc getCameraRay(uint2 threadIdx, float2 sample2)
{
    let pixelCenter = float2(threadIdx.xy) + float2(0.5);
    let screenPos = pixelCenter / DispatchRaysDimensions().xy;
    
    let offset = sample2 / DispatchRaysDimensions().xy;
    
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
    shadowRay.Direction            = es.to;
    shadowRay.TMin                 = k::eps;  
    shadowRay.TMax                 = es.distance - k::eps;// WARN: adhoc

    // payload, true if occluded(hit other object)
    bool occluded = true;
    TraceRay(sceneBVH, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER, ~0, 0, 0, 1, shadowRay, occluded);

    return occluded;
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

    // required for MIS (only for primal hit)
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
        // russian roulette
        {
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
            // NEE (direct light sampling, point or direction)
            if (!bs.isSpecular())
            {
                if (!occluded(si.pos, es)) // Slang (HLSL) does not support short circuit
                {
                    let cosine   = abs(dot(si.normal, es.to));
                    let lightCos = abs(dot(es.normal, -es.to));

                    // if infinite emitter, sampling space is direction (not point) -> jacobian is just 1.0
                    let jacobian = select(es.isInfinite, 1.0, lightCos / (es.distance * es.distance));
                    let G        = cosine * jacobian; // geometric term

                    // calculate BSDF contribution
                    let params = MaterialParams::loadWithTextures(materialParams[si.instanceIndex], textures, texSampler, si.uv);
                    let wo         = si.frame.toLocal(es.to);
                    let f          = DynamicMaterial.BSDF.eval(params, payload.ctx, si.toLocal(), wo);
                    let bsdfPdf    = jacobian * DynamicMaterial.BSDF.pdf(params, payload.ctx, si.toLocal(), wo);
                    let MISWeight  = Warp::heuristic<k::MISHeuristicBeta>(es.pdf, { es.pdf, bsdfPdf });
                    
                    // add contribution to L
                    L += MISWeight * beta * f * G * es.emissive / es.pdf;
                }
            }

            // BSDF sampling (directional sampling)
            { 
                Payload bsdfPayload = Payload(payload.sampler, true);

                RayDesc bsdfRay;
                bsdfRay.Origin      = si.pos;
                bsdfRay.Direction   = si.frame.toWorld(normalize(bs.wo));
                bsdfRay.TMin        = k::eps;
                bsdfRay.TMax        = k::infty;
                TraceRay(sceneBVH, RAY_FLAG_NONE, ~0, 0, 0, 0, bsdfRay, bsdfPayload);

                if (let emissive = bsdfPayload.emissive)
                {
                    uint emitterNum = 0, stride = 0;
                    emitterParams.GetDimensions(emitterNum, stride);
                    let emitterPdf = select(bs.isSpecular(), 0., EmitterSampler.pdf(si, bsdfPayload.si, emitterNum));

                    let cosine      = abs(dot(si.normal, si.frame.toWorld(bs.wo)));
                    let MISWeight   = Warp::heuristic<k::MISHeuristicBeta>(bs.pdf, { emitterPdf, bs.pdf });
                    
                    // add contribution to L
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

    // required only if no explicit sampling(PT) is performed
    if (!enableMIS)
    {
        if (let e = payload.emissive)
        {
            L += beta * e;
        }
    }

    // reject invalid sample
    let invalid = any(isnan(L)) || any(isinf(L));
    return select(invalid, k::zeros.xyz, L);
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
[[vk::binding(9, 0)]] Texture2D<float4> textures[];
[[vk::binding(10, 0)]] SamplerState texSampler;

[shader("raygeneration")]
void rayGenShader()
{
    const static bool kEnableMIS = true; // enable MIS

    uint2 threadIdx = DispatchRaysIndex().xy;
    if (threadIdx.x >= DispatchRaysDimensions().x) return;
    if (threadIdx.y >= DispatchRaysDimensions().y) return;

    let pool = poolImage[threadIdx.xy].xyz;
    let accumulatedSpp = reinterpret<uint>(poolImage[threadIdx.xy].w) + sceneParams.sppPerFrame;

    let pixelSeed = tea(accumulatedSpp, tea(threadIdx.x, threadIdx.y));

    float3 L = float3(0.);

    for (int sampleID = 0; sampleID < sceneParams.sppPerFrame; ++sampleID)
    {
        //L += sampleL<kEnableMIS>(sampleID, pixelSeed) / float(sceneParams.sppPerFrame);
        // for MIS debug
        if (1. * threadIdx.x / DispatchRaysDimensions().x < 1. * threadIdx.y / DispatchRaysDimensions().y)
        {
            L += sampleL<true>(sampleID, pixelSeed) / float(sceneParams.sppPerFrame);
        }
        else
        {
            L += sampleL<false>(sampleID, pixelSeed) / float(sceneParams.sppPerFrame);
        }
    }

    let rate = float(sceneParams.sppPerFrame) / accumulatedSpp;

    let finalRes = lerp(pool, L, rate);
    poolImage[threadIdx.xy] = float4(finalRes, reinterpret<float>(accumulatedSpp));

    let gamma = 2.2;  // HACK: gamma correction
    float3 gammaCorrected = pow(finalRes, 1. / gamma);

    resultImage[threadIdx.xy] = float4(gammaCorrected, 1.0);
    //resultImage[threadIdx.xy] = float4(select(sceneParams.allEmitterNum == 960, k::red, k::blue), 1.0);
}

[shader("miss")]
void missShader(inout Payload payload : SV_RayPayload)
{
    payload.si = none;
    payload.bsdfSample    = none;
    payload.emitterSample = none;
    payload.emissive      = none;

    // infinite emitter
    if (emitterParams[0].type == EmitterType::Infinite)
    {
        if (emitterParams[0].texIndex == -1)  // constant emissive
        {
            payload.emissive = emitterParams[0].emissive;
        }
        else  // envmap
        {
            let dir = normalize(WorldRayDirection());

            // sample the direction of ret.to
            let phi   = k::inv2Pi * sign(dir.z) * acos(dir.x / sqrt(dir.x * dir.x + dir.z * dir.z));
            let theta = k::invPi * acos(dir.y);
            let angle = float2(phi, theta);

            payload.emissive = textures[emitterParams[0].texIndex].Sample(texSampler, angle).xyz;
        }
    }
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
    {
        let p0     = mul(instanceParams[instanceIndex].world, float4(vertices[instanceIndex][index.x].pos, 1.0)).xyz;
        let p1     = mul(instanceParams[instanceIndex].world, float4(vertices[instanceIndex][index.y].pos, 1.0)).xyz;
        let p2     = mul(instanceParams[instanceIndex].world, float4(vertices[instanceIndex][index.z].pos, 1.0)).xyz;
        let area = area(p0, p1, p2);
        payload.si = SurfaceInteraction(worldPos, -worldRayDir, worldNormal, vertex.uv, area, instanceIndex, Frame(worldNormal));
    }

    // sample BSDF and emitter
    MaterialParams params = MaterialParams::loadWithTextures(materialParams[instanceIndex], textures, texSampler, payload.si.value.uv);

    if (any(params.emissive > k::eps))
    {
        payload.emissive          = params.emissive;
    }

    if (payload.skipSampling())
    {
        // just want to know if this surface is emissive
        return;
    }

    payload.bsdfSample = DynamicMaterial.BSDF.sample(params, payload.ctx, payload.si.value.toLocal(), payload.sampler);
    if (let bs = payload.bsdfSample) // update BSDFContext
    {
        payload.ctx.update(bs, params.IOR);
        if (bs.pdf == 0.)
        {
            payload.bsdfSample = none;
        }
    }

    // emitter sample
    payload.emitterSample = EmitterSampler.sample(emitterParams, vertices, indices, instanceParams, textures, texSampler, payload.si.value, payload.sampler);
}

[shader("miss")]
void shadowMissShader(inout bool occluded : SV_RayPayload)
{
    occluded = false;
}