#version 460
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

layout(location = 0) out vec4 outColor;

// layout(binding = 1, set = 0) uniform MaterialUB {
//     vec4 albedo;
//     int texIndex;
//     int matType;
//     float alpha;
//     float IOR;
// } material;

void main()
{
    outColor = vec4(inNormal, 1.0);

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