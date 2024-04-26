#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(binding = 0) uniform sampler2D albedoSampler;
layout(binding = 1) uniform sampler2D normalSampler;
layout(binding = 2) uniform sampler2D worldPosSampler;

layout(location = 0) in vec2 inUV;

layout(location = 0) out vec4 outColor;

void main()
{
    vec4 albedo = texture(albedoSampler, inUV);
    vec3 normal = texture(normalSampler, inUV).xyz;
    vec3 worldPos = texture(worldPosSampler, inUV).xyz;

    if (normal == vec3(0, 0, 0))
    {
        outColor = vec4(1.0, 0, 0, 0);
        return;
    }

    vec4 ambientColor = vec4(0.1, 0.1, 0.1, 1.0);
    vec4 lightDirection = vec4(1.0, 1.0, 1.0, 0.0);
    vec4 eyeDirection = vec4(2.0, 2.0, 2.0, 0.0);

    vec3  invLight  = normalize(lightDirection).xyz;
    vec3  invEye    = normalize(eyeDirection).xyz;
    vec3  halfLE    = normalize(invLight + invEye);
    float diffuse   = clamp(dot(normal, invLight), 0.0, 1.0);
    float specular  = pow(clamp(dot(normal, halfLE), 0.0, 1.0), 30.0);
    vec4  destColor = albedo * vec4(vec3(diffuse), 1.0) + vec4(vec3(specular), 1.0);// + ambientColor;

    outColor = destColor;
    outColor = vec4(vec3(normal), 1.0);
}