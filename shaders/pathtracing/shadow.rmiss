#version 460
#extension GL_EXT_ray_tracing : enable

#include "types.glsl"
#include "constants.glsl"
#include "bindings.glsl"

layout(location = 1) rayPayloadInEXT bool shadowed;

void main()
{
  shadowed = false;
}