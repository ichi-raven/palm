#ifndef TYPES_GLSL_
#define TYPES_GLSL_

precision highp float;
precision highp int;

#extension GL_EXT_buffer_reference : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_shader_explicit_arithmetic_types : enable
#extension GL_EXT_control_flow_attributes : enable

struct InstanceMapping
{
  uint64_t vertexBuffer;
  uint64_t indexBuffer;
  int32_t materialIndex;
  uint32_t padding_instanceMapping[3];
};

struct Material
{
  vec4 albedo;
  vec4 emissive;
  int32_t texIndex;
  int32_t matType;
  float alpha;
  float IOR;
};

struct DisneyMaterial
{
  vec3 baseColor;
  float metallic;
  float roughness;
  float flatness;
  vec3 emissive;
    
  float specularTint;
  float specTrans;
  float diffTrans;
  float ior;
  float relativeIOR;
  float absorption;

  float sheen;
  vec3 sheenTint;
  float anisotropic;

  float clearcoat;
  float clearcoatGloss;
};

struct DisneyBSDFState
{
  bool isRefracted;
  bool hasBeenRefracted;
  float lastIOR;
};

struct Vertex 
{
  vec3 position;
  vec3 normal;
  vec2 texCoord;
  vec4 joint;
  vec4 weight;
};

struct Ray
{
  vec3 origin;
  vec3 direction;
};

struct BSDFSample
{
  vec3 f;
  vec3 wi;
  float forwardPdfW;
  float reversePdfW;
  uint flags;
};

struct LightSample
{
  vec3 on;
  vec3 to;
  vec3 L;
  float pdf;
};

struct Payload
{
  vec3 x;
  vec3 normal;
  BSDFSample bsdf;
  vec3 Le;
  uint prngState;
  bool intersected;

  // for Disney BSDF
  DisneyBSDFState state;
  DisneyMaterial mat;
};

struct ONB
{
  vec3 u;
  vec3 v;
  vec3 w;
};

#endif