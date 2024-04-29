#version 450

#include "../common/types.glsl"
#include "../common/constants.glsl"
#include "../common/DisneyBSDF.glsl"

layout(location = 0) in vec3 fragPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;

layout(location = 0) out vec4 outAlbedo;
layout(location = 1) out vec4 outNormal;
layout(location = 2) out vec4 outWorldPos;

layout(binding = 0, set = 1) uniform InstanceUB {
    mat4 model;
    uint matIndex;
    vec3 padding;
} instanceUB;

layout(binding=1, set=0) readonly buffer Materials { Material materials[]; };
layout(binding=2, set=0) uniform sampler2D texSamplers[];

void main() 
{ 
    //Material material = materials[instanceUB.matIndex];
    outAlbedo = vec4(float(instanceUB.matIndex), 0, 0, 0);
    outNormal = vec4(fragNormal, 1.0);
    outWorldPos = vec4(fragPos, 1.0);
}