

import "../Sampler/Sampler";
import "../Material/Material";
import "../Emitter/Emitter";
import "../Utility/SurfaceInteraction";
import "../Utility/Frame";
import "../Utility/Warp";
import "../Utility/Constants";
import "../Utility/Reservoir";
import "../Utility/Color";

struct SceneParams
{
    float4x4 view;
    float4x4 proj;
    float4x4 viewInv;
    float4x4 projInv;
    float4 cameraPos;

    uint32_t sppPerFrame;
    uint32_t allEmitterNum;
    uint32_t M;
    uint32_t padding;
}

struct InstanceParams : IInstance
{
    float4x4 world;
    float4x4 worldInvTrans;
}

struct Payload //<Sampler: ISampler> TODO: selecting sampler
{
    __init(const IndependentSampler sampler_, const bool sampleOnlyEmissive_ = false, const bool sampleEmitter_ = true)
    {
        ctx = BSDFContext();
        sampler = sampler_;
        sampleOnlyEmissive = sampleOnlyEmissive_;
        sampleEmitter = sampleEmitter_;
        resetForNextBounce();
    }

    Optional<SurfaceInteraction> si;
    Optional<BSDFSample> bsdfSample;
    Optional<EmitterSample> emitterSample;
    Optional<float3> emissive;
    
    BSDFContext ctx;
    IndependentSampler sampler;
    bool sampleOnlyEmissive;
    bool sampleEmitter;

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
    shadowRay.Direction            = normalize(es.to);
    shadowRay.TMin                 = k::eps;  
    shadowRay.TMax                 = es.distance - k::eps;// WARN: adhoc

    // payload, true if occluded(hit other object)
    bool occluded = true;
    TraceRay(sceneBVH, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER, ~0, 0, 0, 1, shadowRay, occluded);

    return occluded;
}

Payload samplePrimalHit(in int sampleID, in int pixelSeed)
{
    let seed        = tea(sampleID, pixelSeed);
    Payload payload = Payload(IndependentSampler(seed));

    // trace primary ray
    RayDesc ray = getCameraRay(DispatchRaysIndex().xy, payload.sampler.next2D());
    TraceRay(sceneBVH, RAY_FLAG_NONE, ~0, 0, 0, 0, ray, payload);
    
    return payload;
}

Tuple<float3, float3> sampleDI(inout Payload payload, const EmitterSample es)
{
    float3 L    = select(payload.emissive.hasValue, payload.emissive.value, float3(0.0));
    float3 beta = float3(1.0);// WARN: does not take into account the initial weight(e.g. sensor sampling)

    if (!payload.continue())
    {
        return makeTuple(k::zeros.xyz, k::zeros.xyz);  // terminate path
    }
    
    let si = payload.si.value;
    let bs = payload.bsdfSample.value;

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
    
    
    // update parameters
    beta *= bs.f * abs(dot(si.normal, si.frame.toWorld(bs.wo))) / bs.pdf;

    // reject invalid sample
    let invalid = any(isnan(L)) || any(isinf(L));
    return makeTuple(select(invalid, k::zeros.xyz, L), beta);
}

float3 sampleGI(in int sampleID, in int pixelSeed, in Payload payload_, in float3 beta_)
{
    float3 L    = float3(0.0);
    float3 beta = beta_;

    RayDesc ray     = {};
    Payload payload = payload_;

    // trace ray recursively
    for (int depth = 3; depth < k::maxDepth && payload.continue(); ++depth)
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

        // NEE (direct light sampling, point or direction)
        if (!bs.isSpecular())
        {
            if (!occluded(si.pos, es))  // Slang (HLSL) does not support short circuit
            {
                let cosine   = abs(dot(si.normal, es.to));
                let lightCos = abs(dot(es.normal, -es.to));

                // if infinite emitter, sampling space is direction (not point) -> jacobian is just 1.0
                let jacobian = select(es.isInfinite, 1.0, lightCos / (es.distance * es.distance));
                let G        = cosine * jacobian;  // geometric term

                // calculate BSDF contribution
                let params    = MaterialParams::loadWithTextures(materialParams[si.instanceIndex], textures, texSampler, si.uv);
                let wo        = si.frame.toLocal(es.to);
                let f         = DynamicMaterial.BSDF.eval(params, payload.ctx, si.toLocal(), wo);
                let bsdfPdf   = jacobian * DynamicMaterial.BSDF.pdf(params, payload.ctx, si.toLocal(), wo);
                let MISWeight = Warp::heuristic<k::MISHeuristicBeta>(es.pdf, { es.pdf, bsdfPdf });

                // add contribution to L
                L += MISWeight * beta * f * G * es.emissive / es.pdf;
            }
        }

        // BSDF sampling (directional sampling)
        {
            Payload bsdfPayload = Payload(payload.sampler, true);

            RayDesc bsdfRay;
            bsdfRay.Origin    = si.pos;
            bsdfRay.Direction = si.frame.toWorld(normalize(bs.wo));
            bsdfRay.TMin      = k::eps;
            bsdfRay.TMax      = k::infty;
            TraceRay(sceneBVH, RAY_FLAG_NONE, ~0, 0, 0, 0, bsdfRay, bsdfPayload);

            if (let emissive = bsdfPayload.emissive)
            {
                uint emitterNum = 0, stride = 0;
                emitterParams.GetDimensions(emitterNum, stride);
                let emitterPdf = select(bs.isSpecular(), 0., EmitterSampler.pdf(si, bsdfPayload.si, emitterNum));

                let cosine    = abs(dot(si.normal, si.frame.toWorld(bs.wo)));
                let MISWeight = Warp::heuristic<k::MISHeuristicBeta>(bs.pdf, { emitterPdf, bs.pdf });

                // add contribution to L
                L += MISWeight * beta * bs.f * cosine * emissive / bs.pdf;
            }

            // reflects sampler transitions
            payload.sampler = bsdfPayload.sampler;
        }
        

        // update parameters
        beta *= bs.f * abs(dot(si.normal, si.frame.toWorld(bs.wo))) / bs.pdf;

        ray.Origin    = si.pos;
        ray.Direction = si.frame.toWorld(bs.wo);
        ray.TMin      = k::eps;
        ray.TMax      = k::infty;
        payload.resetForNextBounce();

        // trace next ray
        TraceRay(sceneBVH, RAY_FLAG_NONE, ~0, 0, 0, 0, ray, payload);
    }

    // reject invalid sample
    let invalid = any(isnan(L)) || any(isinf(L));
    return select(invalid, k::zeros.xyz, L);
}

Reservoir<EmitterSample> sampleReservoirs<S : ISampler>(const uint2 threadIdx, const uint seed, const Payload payload, const uint M, inout S sampler)
{
    var r = reservoirs[threadIdx.x + threadIdx.y * DispatchRaysDimensions().x];

    let si = payload.si.value;
    let bs = payload.bsdfSample.value;

    [unroll]
    for (int i = 0; i < M; ++i)
    {
        EmitterSample es = EmitterSampler.sample(emitterParams, vertices, indices, instanceParams, textures, texSampler, si, sampler);
        if (occluded(si.pos, es))
        {
            continue; // skip if occluded
        }

        let cosine       = abs(dot(si.normal, es.to));
        let lightCos     = abs(dot(es.normal, -es.to));
        let jacobian     = select(es.isInfinite, 1.0, lightCos / (es.distance * es.distance));
        let G            = cosine * jacobian;  // geometric term
        let u            = sampler.next1D();
        let target       = toGray(bs.f * es.emissive * G); // BSDF contribution

        // update reservoir
        r.update(es, target / es.pdf, es.pdf, u);
    }

    sizeof(Reservoir<EmitterSample>);

    return reservoirs[threadIdx.x + threadIdx.y * DispatchRaysDimensions().x] = r;
}

float3 shadeGI(const uint2 threadIdx, const uint pixelSeed, const Payload payload, const float3 beta)
{
    float3 L   = float3(0.);
    let invSpp = 1. / float(sceneParams.sppPerFrame);

    [unroll]
    for (int sampleID = 0; sampleID < sceneParams.sppPerFrame; ++sampleID)
    {
        L += invSpp * sampleGI(sampleID, pixelSeed, payload, beta);
    }

    return L;
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
[[vk::binding(11, 0)]] RWStructuredBuffer<Reservoir<EmitterSample>> reservoirs;
[[vk::binding(12, 0)]] RWTexture2D DIImage;
[[vk::binding(13, 0)]] RWTexture2D GIImage;

[shader("raygeneration")]
void rayGenShader()
{
    static const bool kEnableMIS = true;
    static const float kGamma     = 2.2;

    uint2 threadIdx = DispatchRaysIndex().xy;
    if (threadIdx.x >= DispatchRaysDimensions().x) return;
    if (threadIdx.y >= DispatchRaysDimensions().y) return;

    let accumulatedSpp = reinterpret<uint>(poolImage[threadIdx.xy].w) + sceneParams.sppPerFrame;

    let pixelSeed = tea(accumulatedSpp, tea(threadIdx.x, threadIdx.y));

    // create G-Buffer
    Payload payload = samplePrimalHit(accumulatedSpp, pixelSeed);
    
    if (!payload.continue())
    {
        // if no valid hit, just return
        let envmap                = select(payload.emissive.hasValue, payload.emissive.value, k::black);
        poolImage[threadIdx.xy]   = float4(envmap, reinterpret<float>(accumulatedSpp));
        resultImage[threadIdx.xy] = float4(linearToGamma(envmap, kGamma), 1.0);
        return;
    }

    // save G-buffer for denoising

    // DEBUG
    //resultImage[threadIdx.xy] = float4(payload.bsdfSample.value.f, 1.0);  // write position to result image
    //resultImage[threadIdx.xy] = select(sizeof(Reservoir<EmitterSample>) == 64, float4(k::red, 1.0), float4(k::blue, 1.0)); // write green if reservoir is empty
    //return;

    sampleReservoirs(threadIdx, pixelSeed, payload, sceneParams.M, payload.sampler);

    // TODO: spatio-temporal reuse

    let mergedReservoir = reservoirs[threadIdx.x + threadIdx.y * DispatchRaysDimensions().x];
    var candidate       = mergedReservoir.candidate;
    candidate.emissive *= mergedReservoir.weight();

    let sampledDI = sampleDI(payload, candidate);
    let DI        = sampledDI._0;
    let GI        = shadeGI(threadIdx, pixelSeed, payload, sampledDI._1);

    let accumulatedDI = reinterpret<float>(DIImage[threadIdx.xy].w) + 1.;
    let DIRate        = 1. / accumulatedDI;
    let finalDI       = DI;//lerp(DIImage[threadIdx.xy].xyz, DI, DIRate);
    DIImage[threadIdx.xy]   = float4(finalDI, reinterpret<float>(accumulatedDI));

    
    // TODO: denoise DI and GI in secondary pass -> write final result
    let rate                = float(sceneParams.sppPerFrame) / accumulatedSpp;
    let finalGI             = lerp(GIImage[threadIdx.xy].xyz, GI, rate);
    let L                   = finalDI + finalGI;
    //let pool                = poolImage[threadIdx.xy].xyz;

    GIImage[threadIdx.xy]   = float4(finalGI, reinterpret<float>(accumulatedSpp));
    poolImage[threadIdx.xy] = float4(L, reinterpret<float>(accumulatedSpp));

    resultImage[threadIdx.xy] = float4(linearToGamma(L, kGamma), 1.0);
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
    if (payload.sampleEmitter)
    {
        payload.emitterSample = EmitterSampler.sample(emitterParams, vertices, indices, instanceParams, textures, texSampler, payload.si.value, payload.sampler);
    }
}

[shader("miss")]
void shadowMissShader(inout bool occluded : SV_RayPayload)
{
    occluded = false;
}