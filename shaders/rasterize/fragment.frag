#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_EXT_nonuniform_qualifier : enable

#include "../common/types.glsl"
#include "../common/constants.glsl"
#include "../common/DisneyBSDF.glsl"

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

layout(location = 0) out vec4 outColor;

layout(binding = 0, set = 0) uniform SceneUB {
    mat4 view;
    mat4 proj;
    vec4 camPos;
} sceneUB;


layout(binding = 0, set = 1) uniform InstanceUB {
    mat4 model;
    uint matIndex;
    vec3 padding;
} instanceUB;

layout(binding=1, set=0) readonly buffer Materials { Material materials[]; };
layout(binding=2, set=0) uniform sampler2D texSamplers[];

void main()
{
    Material material = materials[instanceUB.matIndex];


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
    vec3 disneyRes = EvaluateDisneyBSDF(disneyMat, inPos - sceneUB.camPos.xyz, lightDirection, inNormal, false, fpdf, rpdf);

    outColor = vec4(disneyRes, 1.0);

    //outColor = vec4(material.albedo, 1.0);
    //outColor = vec4(material.emissive);

    // vec4 albedo = material.albedo;

    // if (inNormal == vec3(0, 0, 0))
    // {
    //     outColor = vec4(1.0, 0, 0, 0);
    //     return;
    // }

    // vec4 ambientColor = vec4(0.1, 0.1, 0.1, 1.0);
    // vec4 lightDirection = vec4(1.0, 1.0, 1.0, 0.0);
    // vec4 eyeDirection = vec4(2.0, 2.0, 2.0, 0.0);

    // vec3  invLight  = normalize(lightDirection).xyz;
    // vec3  invEye    = normalize(eyeDirection).xyz;
    // vec3  halfLE    = normalize(invLight + invEye);
    // float diffuse   = clamp(dot(inNormal, invLight), 0.0, 1.0);
    // float specular  = pow(clamp(dot(inNormal, halfLE), 0.0, 1.0), 30.0);
    // vec4  destColor = albedo * vec4(vec3(diffuse), 1.0) + vec4(vec3(specular), 1.0);// + ambientColor;

    // outColor = destColor;
}