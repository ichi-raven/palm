#version 450

layout(binding = 1) uniform sampler2D texSampler;

layout(location = 0) in vec3 fragPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;

layout(location = 0) out vec4 outAlbedo;
layout(location = 1) out vec4 outNormal;
layout(location = 2) out vec4 outWorldPos;

void main() 
{
    outAlbedo = texture(texSampler, fragTexCoord);
    outNormal = vec4(fragNormal, 1.0);
    outWorldPos = vec4(fragPos, 1.0);
}