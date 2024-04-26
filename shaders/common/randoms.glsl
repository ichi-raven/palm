#ifndef RANDOMS_GLSL_
#define RANDOMS_GLSL_

precision highp float;
precision highp int;

#extension GL_EXT_buffer_reference : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_shader_explicit_arithmetic_types : enable
#extension GL_EXT_control_flow_attributes : enable

float stepAndOutputRNGFloat(inout uint rngState)
{
  // Condensed version of pcg_output_rxs_m_xs_32_32, with simple conversion to floating-point [0,1].
  rngState  = rngState * 747796405 + 1;
  uint word = ((rngState >> ((rngState >> 28) + 4)) ^ rngState) * 277803737;
  word      = (word >> 22) ^ word;
  return float(word) / 4294967295.0f;
}

float stepAndOutputRNGFloatInRange(const float from, const float to, inout uint rngState)
{
  // Condensed version of pcg_output_rxs_m_xs_32_32, with simple conversion to floating-point [0,1].
  rngState  = rngState * 747796405 + 1;
  uint word = ((rngState >> ((rngState >> 28) + 4)) ^ rngState) * 277803737;
  word      = (word >> 22) ^ word;
  return float(word) / 4294967295.0f * (to - from) + from;
}

// Generate a random unsigned int from two unsigned int values, using 16 pairs
// of rounds of the Tiny Encryption Algorithm. See Zafar, Olano, and Curtis,
// "GPU Random Numbers via the Tiny Encryption Algorithm"
uint tea(const uint val0, const uint val1)
{
  uint v0 = val0;
  uint v1 = val1;
  uint s0 = 0;

  [[unroll]]
  for(uint n = 0; n < 16; n++)
  {
    s0 += 0x9e3779b9;
    v0 += ((v1 << 4) + 0xa341316c) ^ (v1 + s0) ^ ((v1 >> 5) + 0xc8013ea4);
    v1 += ((v0 << 4) + 0xad90777d) ^ (v0 + s0) ^ ((v0 >> 5) + 0x7e95761e);
  }

  return v0;
}

vec3 randomUnitVector(inout uint randState) 
{
  float a = stepAndOutputRNGFloat(randState) * 2. * M_PI;
  float z = stepAndOutputRNGFloat(randState) * 2. - 1.;
  float r = sqrt(1 - z * z);
  return vec3(r * cos(a), r * sin(a), z);
}

vec3 randomCosDirection(inout uint randState)
{
  const float r1 = stepAndOutputRNGFloat(randState);
  const float r2 = stepAndOutputRNGFloat(randState);
  const float r2Sqrt = sqrt(r2);
  const float z = sqrt(1 - r2);
  const float phi = M_PI2 * r1;

  return vec3(cos(phi) * r2Sqrt, sin(phi) * r2Sqrt, z);
}

#endif