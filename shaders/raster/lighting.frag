#version 450
#extension GL_ARB_separate_shader_objects : enable

#include "../common/types.glsl"
#include "../common/constants.glsl"
#include "../common/DisneyBSDF.glsl"

layout(binding = 0, set = 0) uniform sampler2D albedoSampler;
layout(binding = 1, set = 0) uniform sampler2D normalSampler;
layout(binding = 2, set = 0) uniform sampler2D worldPosSampler;

layout(binding = 0, set = 1) uniform SceneUB {
    mat4 view;
    mat4 proj;
    vec4 camPos;
} sceneUB;

layout(binding=1, set=1) readonly buffer Materials { Material materials[]; };
layout(binding=2, set=1) uniform sampler2D texSamplers[];

layout(location = 0) in vec2 inUV;

layout(location = 0) out vec4 outColor;

void main()
{
    vec4 albedo = texture(albedoSampler, inUV);
    vec3 normal = texture(normalSampler, inUV).xyz;
    vec3 worldPos = texture(worldPosSampler, inUV).xyz;

    uint matIndex = uint(albedo.x);

    if (normal == vec3(0, 0, 0))
    {
        outColor = vec4(0.2, 0.2, 0.2, 0);
        return;
    }

    Material material = materials[matIndex];
    // outColor = material.albedo;
    // return;

    DisneyMaterial disneyMat;
    disneyMat.baseColor = material.albedo.xyz;
    disneyMat.metallic = 0.4;
    disneyMat.roughness = 0.01;
    disneyMat.flatness = 1.0;
    disneyMat.emissive = material.emissive.xyz;
  
    disneyMat.specularTint = 0.1;
    disneyMat.specTrans = 0.0;
    disneyMat.diffTrans = 0.0;
    disneyMat.ior = material.IOR;
    disneyMat.relativeIOR = material.IOR;
    disneyMat.absorption = 0.0;

    disneyMat.sheen = 0.1;
    disneyMat.sheenTint = vec3(0.1);
    disneyMat.anisotropic = 0.01;

    disneyMat.clearcoat = 0.1;
    disneyMat.clearcoatGloss = 0.1;
    
    vec3 lightDirection = normalize(vec3(1.0, 1.0, 1.0));
    float fpdf, rpdf;
    vec3 disneyRes = EvaluateDisneyBSDF(disneyMat, worldPos - sceneUB.camPos.xyz, lightDirection, normal, false, fpdf, rpdf);

    outColor = vec4(disneyRes, 1.0);

    // Phong shading for debug

    // vec4 ambientColor = vec4(0.1, 0.1, 0.1, 1.0);
    // vec4 lightDirection = vec4(-1.0, -1.0, 1.0, 0.0);
    // vec4 eyeDirection = sceneUB.camPos;

    // vec3  invLight  = normalize(lightDirection).xyz;
    // vec3  invEye    = normalize(eyeDirection).xyz;
    // vec3  halfLE    = normalize(invLight + invEye);
    // float diffuse   = clamp(dot(normal, invLight), 0.0, 1.0);
    // float specular  = pow(clamp(dot(normal, halfLE), 0.0, 1.0), 30.0);
    // vec4  destColor = albedo * vec4(vec3(diffuse), 1.0) + vec4(vec3(specular), 1.0);// + ambientColor;

    // outColor = destColor;
}