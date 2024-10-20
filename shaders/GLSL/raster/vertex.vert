#version 460

layout(binding = 0, set = 0) uniform SceneUB {
    mat4 view;
    mat4 proj;
} sceneUB;

layout(binding = 0, set = 1) uniform InstanceUB {
    mat4 model;
    uint matIndex;
    vec3 padding;
} instanceUB;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec4 injoint;
layout(location = 4) in vec4 inWeight;

layout(location = 0) out vec3 fragPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragTexCoord;

void main() 
{
    vec4 worldPos = instanceUB.model * vec4(inPosition, 1.0);

    gl_Position = sceneUB.proj * sceneUB.view * worldPos;
    fragPos = worldPos.xyz;
    fragNormal = (inverse(transpose(instanceUB.model)) * vec4(inNormal, 1.0)).xyz;
    fragTexCoord = inTexCoord;
}