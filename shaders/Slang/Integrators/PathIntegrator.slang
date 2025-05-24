

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

struct Payload //<Sampler: ISampler> TODO: selecting sampler
{
    __init(const IndependentSampler sampler_, const bool sampleOnlyEmissive_ = false)
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
    Optional<uint> hitSurfaceFaceNum;

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

    // payload, true if hit other object
    bool ret = true;
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

    // required for MIS (for primal hit)
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
            // NEE (direct light sampling)
            if (!bs.isSpecular()) 
            {
                if (!occluded(si.pos, es)) // Slang (HLSL) does not support short circuit
                {
                    MaterialParams params = MaterialParams::loadWithTextures(materialParams[si.instanceIndex], textures, texSampler, si.uv);

                    // add emitter directional contribution to L
                    let wo = si.frame.toLocal(normalize(es.to));
                    let lightCos = abs(dot(normalize(es.to), es.normal));
                    let cosine = abs(dot(si.normal, normalize(es.to)));

                    // if infinite emitter, G term will be infinite
                    let G = (es.isInfinite ? 1. : lightCos / (es.distance * es.distance));

                    // calculate BSDF contribution
                    let f         = DynamicMaterial.BSDF.eval(params, payload.ctx, si.toLocal(), wo);
                    let pathpdf   = G * DynamicMaterial.BSDF.pdf(params, payload.ctx, si.toLocal(), wo);
                    // balance heuristic
                    let MISWeight = es.pdf / (es.pdf + pathpdf);

                    L += MISWeight * beta * f * G * cosine * es.emissive / es.pdf;
                }                                                                                                                                                    
            }

            // BSDF sampling
            //if (false)
            {
                Payload bsdfPayload = Payload(payload.sampler, true);

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
                            uint size   = sceneParams.areaEmitterNum;
                            uint stride = 0;
                            emitterParams.GetDimensions(size, stride);
                            if (let faceNum = bsdfPayload.hitSurfaceFaceNum)
                            {
                                size *= faceNum;
                            }

                            emitterpdf = EmitterSampler.pdf(si, bsdfSi, size);
                        }
                    }

                    let cosine    = abs(dot(si.normal, si.frame.toWorld(bs.wo)));
                    // balance heuristic
                    let MISWeight = bs.pdf / (emitterpdf + bs.pdf);
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
[[vk::binding(9, 0)]] Texture2D<float4> textures[];
[[vk::binding(10, 0)]] SamplerState texSampler;

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
    let area = area(vertices[instanceIndex][index.x].pos, vertices[instanceIndex][index.y].pos, vertices[instanceIndex][index.z].pos);
    payload.si = SurfaceInteraction(worldPos, -worldRayDir, worldNormal, vertex.uv, area, instanceIndex, Frame(worldNormal));

    // sample BSDF and emitter
    MaterialParams params = MaterialParams::loadWithTextures(materialParams[instanceIndex], textures, texSampler, payload.si.value.uv);

    if (any(params.emissive > 0.))
    {
        uint size = 0, stride = 0;
        indices[instanceIndex].GetDimensions(size, stride);
        
        payload.emissive          = params.emissive;
        payload.hitSurfaceFaceNum = size / 3; // number of faces in this instance
    }

    if (payload.skipSampling())
    {
        // just want to know if this surface is emissive
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
    //payload.emitterSample = EmitterSampler.sample<Vertex, InstanceParams, IndependentSampler>(emitterParams, vertices, indices, instanceParams, textures, texSampler, payload.si.value, payload.sampler.next1D(), payload.sampler.next2D(), payload.sampler.next1D());
    payload.emitterSample = EmitterSampler.sample<Vertex, InstanceParams, IndependentSampler>(emitterParams, vertices, indices, instanceParams, textures, texSampler, payload.si.value, payload.sampler);
}

[shader("miss")]
void shadowMissShader(inout bool occluded : SV_RayPayload)
{
    occluded = false;
}