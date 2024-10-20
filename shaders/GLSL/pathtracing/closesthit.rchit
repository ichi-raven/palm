#version 460
#extension GL_EXT_ray_tracing : enable
#extension GL_EXT_buffer_reference : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_shader_explicit_arithmetic_types : enable
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_control_flow_attributes : enable

hitAttributeEXT vec3 attribs;

#include "../common/types.glsl"
#include "../common/constants.glsl"
#include "../common/randoms.glsl"
#include "../common/BSDFs.glsl"
#include "../common/DisneyBSDF.glsl"
#include "../common/lights.glsl"

#include "bindings.glsl"


layout(location = 0) rayPayloadInEXT Payload payload;

layout(buffer_reference, buffer_reference_align = 4, scalar) readonly buffer VertexBuffer 
{
    Vertex v[];
};

layout(buffer_reference, scalar) readonly buffer IndexBuffer 
{
    uvec3 i[];
};

Vertex FetchVertexInterleaved(
  vec3 barys,
  uint64_t vertexBuffer,
  uint64_t indexBuffer)
{
  IndexBuffer indices = IndexBuffer(indexBuffer);
  VertexBuffer verts = VertexBuffer(vertexBuffer);

  const uvec3 idx = indices.i[gl_PrimitiveID].xyz;
  Vertex v0 = verts.v[idx.x];
  Vertex v1 = verts.v[idx.y];
  Vertex v2 = verts.v[idx.z];

  Vertex v;
  v.position = v0.position * barys.x + v1.position * barys.y + v2.position * barys.z;
  v.normal = normalize(v0.normal * barys.x + v1.normal * barys.y + v2.normal * barys.z);
  v.texCoord = v0.texCoord * barys.x + v1.texCoord * barys.y + v2.texCoord * barys.z;
  v.joint = v0.joint * barys.x + v1.joint * barys.y + v2.joint * barys.z;
  v.weight = v0.weight * barys.x + v1.weight * barys.y + v2.weight * barys.z;

  return v;
}

void main() 
{

  const vec3 barys = vec3(1.0 - attribs.x - attribs.y, attribs.x, attribs.y);
  const InstanceMapping mapping = instanceMappings[gl_InstanceID];
  const Vertex vtx = FetchVertexInterleaved(barys, mapping.vertexBuffer, mapping.indexBuffer);

  Material material = materials[nonuniformEXT(mapping.materialIndex)];
  if (material.texIndex != -1)
  {
    material.albedo = texture(texSamplers[nonuniformEXT(material.texIndex)], vtx.texCoord);
  }

  const vec3 worldPos    = (gl_ObjectToWorldEXT * vec4(vtx.position, 1.0)).xyz;
  const vec3 worldNormal = normalize(mat3(gl_ObjectToWorldEXT) * vtx.normal);

  payload.x = worldPos;
  payload.normal = setFaceNormal(-gl_WorldRayDirectionEXT, worldNormal);
  payload.Le = material.emissive.xyz;
  payload.intersected = true;
  
  // for simple BSDF
  // payload.bsdf = sampleBSDF(material, -gl_WorldRayDirectionEXT, worldNormal, payload.prngState);
  //return;

  // test Disney BSDF
  
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
  disneyMat.relativeIOR = payload.state.lastIOR / material.IOR;
  disneyMat.absorption = 0.0;

  disneyMat.sheen = 0.1;
  disneyMat.sheenTint = vec3(0.1);
  disneyMat.anisotropic = 0.01;

  disneyMat.clearcoat = 0.1;
  disneyMat.clearcoatGloss = 0.1;

  /*switch(material.matType)
  {
    // case MAT_LAMBERT:
    //   disneyMat.roughness = 1.0;
    // break;
    case MAT_CONDUCTOR:
      disneyMat.roughness = 0.01;
      //const float rate = 0.6;//float((sceneParams.frame / 20) % 10) / 10.0;
      //disneyMat.anisotropic = mix(-1.0, 1.0, rate);
      disneyMat.anisotropic = 0.5;
      disneyMat.metallic = 1.0;
      disneyMat.clearcoat = 0.1;
      disneyMat.clearcoatGloss = 0.1;
      disneyMat.flatness = 0.0;
    break;
    case MAT_DIELECTRIC:
      disneyMat.roughness = 0.001;
      disneyMat.metallic = 0.01;
      disneyMat.specTrans = 1.0;
      disneyMat.diffTrans = 1.0;
      disneyMat.anisotropic = 0.0;
      disneyMat.clearcoat = 0.1;
      disneyMat.clearcoatGloss = 0.0;
      disneyMat.flatness = 0.0;
    break;
    default:
    // ERROR
    break;
  }*/

  //BSDFSample sampleDisneyBSDF(const DisneyMaterial mat, const vec3 x, const vec3 normal, bool thin, inout DisneyBSDFState state, inout uint prngState)

  payload.bsdf = sampleDisneyBSDF(disneyMat, -gl_WorldRayDirectionEXT, worldNormal, false, payload.state, payload.prngState);
  payload.state.lastIOR = material.IOR;
  payload.mat = disneyMat;

  // DEBUG
  //payload.Le = payload.bsdf.f;
  //payload.intersected = false;
  return;
  
}
