#ifndef CONSTANTS_GLSL_
#define CONSTANTS_GLSL_

precision highp float;
precision highp int;

#extension GL_EXT_buffer_reference : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_shader_explicit_arithmetic_types : enable
#extension GL_EXT_control_flow_attributes : enable

// constants
#define EPS (1e-3)
#define INFTY (1e8)

#define M_PI  (3.1415926535897932384626433832795)
#define M_PI2 (6.28318530718)
#define M_INVPI (0.31830988618)

#define MAT_LAMBERT (0)
#define MAT_CONDUCTOR (1)
#define MAT_DIELECTRIC (2)

#define RED (vec3(0.8, 0.2, 0.2))
#define GREEN (vec3(0.2, 0.8, 0.2))
#define BLUE (vec3(0.2, 0.2, 0.8))
#define MAGENTA (vec3(0.8, 0.2, 0.8))
#define CYAN (vec3(0.2, 0.8, 0.8))
#define YELLOW (vec3(0.8, 0.8, 0.2))
#define BLACK (vec3(0.0))
#define WHITE (vec3(1.0))

#define MAX_DEPTH (8)

#define BSDF_FLAGS_FAILED       (0)
#define BSDF_FLAGS_REFLECTION   (1 << 0)
#define BSDF_FLAGS_TRANSMISSION (1 << 1)
#define BSDF_FLAGS_DIFFUSE      (1 << 2)
#define BSDF_FLAGS_GLOSSY       (1 << 3)
#define BSDF_FLAGS_SPECULAR     (1 << 4)

#define BSDF_FLAGS_DIFFUSE_REFLECTION       (BSDF_FLAGS_DIFFUSE | BSDF_FLAGS_REFLECTION)
#define BSDF_FLAGS_DIFFUSE_TRANSMISSION     (BSDF_FLAGS_DIFFUSE | BSDF_FLAGS_TRANSMISSION)
#define BSDF_FLAGS_GLOSSY_REFLECTION        (BSDF_FLAGS_GLOSSY | BSDF_FLAGS_REFLECTION)
#define BSDF_FLAGS_GLOSSY_TRANSMISSION      (BSDF_FLAGS_GLOSSY | BSDF_FLAGS_TRANSMISSION)
#define BSDF_FLAGS_SPECULAR_REFLECTION      (BSDF_FLAGS_SPECULAR | BSDF_FLAGS_REFLECTION)
#define BSDF_FLAGS_SPECULAR_TRANSMISSION    (BSDF_FLAGS_SPECULAR | BSDF_FLAGS_TRANSMISSION)

#define BSDF_FLAGS_ALL                      (BSDF_FLAGS_REFLECTION | BSDF_FLAGS_GLOSSY | BSDF_FLAGS_SPECULAR | BSDF_FLAGS_REFLECTION | BSDF_FLAGS_TRANSMISSION)

const float tmin = EPS;
const float tmax = INFTY;

#endif