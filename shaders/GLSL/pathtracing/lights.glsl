#ifndef LIGHTS_GLSL_
#define LIGHTS_GLSL_

precision highp float;
precision highp int;

#extension GL_EXT_buffer_reference : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_shader_explicit_arithmetic_types : enable
#extension GL_EXT_control_flow_attributes : enable

#include "types.glsl"
#include "constants.glsl"
#include "randoms.glsl"


LightSample sampleLight(const vec3 pos, inout uint prngState)
{
    LightSample ls;
    ls.pdf = 1.0;
    ls.L = vec3(15.0);

    // Debug: for direct light sampling
    vec3 debugLight[4] = {vec3(0.2300, 1.5800, -0.2200), vec3(0.2300, 1.5800, 0.1600), vec3(-0.2400, 1.5800, 0.1600), vec3(-0.2400, 1.5800, -0.2200)};    
    const float xRange[2] = {debugLight[2].x, debugLight[0].x};
    const float zRange[2] = {debugLight[0].z, debugLight[1].z};
    const float lightArea = (xRange[1] - xRange[0]) * (zRange[1] - zRange[0]);
    const vec3 lightNormal = vec3(0.0, -1.0, 0.0);

    // sample direct illumination
    const float r1 = stepAndOutputRNGFloat(prngState);
    const float r2 = stepAndOutputRNGFloat(prngState);
    const float randLightX = clamp(r1 * (xRange[1] - xRange[0]) + xRange[0], xRange[0], xRange[1]);
    const float randLightZ = clamp(r2 * (zRange[1] - zRange[0]) + zRange[0], zRange[0], zRange[1]);
    ls.on = vec3(randLightX, debugLight[0].y, randLightZ);
    //ls.on = debugLight[1];

    ls.to = ls.on - pos;
    const float distSq = dot(ls.to, ls.to);

    const float lightCos = abs(dot(normalize(ls.to), lightNormal));
    
    ls.pdf = max(EPS, distSq / (lightCos * lightArea));

    return ls;
}

#endif