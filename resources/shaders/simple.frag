#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require

#include "common.h"

layout(location = 0) out vec4 out_fragColor;

layout (location = 0 ) in VS_OUT
{
    vec3 wPos;
    vec3 wNorm;
    vec3 wTangent;
    vec2 texCoord;
} surf;

layout(binding = 0, set = 0) uniform AppData
{
    UniformParams Params;
};
layout(binding = 1, set = 0) buffer ffFinalBuf { float ff[]; };


void main()
{
    uvec3 voxelCoord = uvec3(floor((surf.wPos - Params.bmin) / Params.voxelSize));
    uvec3 voxelsExtend = uvec3(ceil((Params.bmax - Params.bmin) / Params.voxelSize));
    uint voxelsCount = voxelsExtend.x * voxelsExtend.y * voxelsExtend.z;
    uint voxelIdx = (voxelCoord.x * voxelsExtend.y + voxelCoord.y) * voxelsExtend.z + voxelCoord.z;
    vec3 lightDir1 = normalize(Params.lightPos.xyz - surf.wPos);
    vec3 lightDir2 = vec3(0.0f, 0.0f, 1.0f);

    const vec4 dark_violet = vec4(0.59f, 0.0f, 0.82f, 1.0f);
    const vec4 chartreuse  = vec4(0.5f, 1.0f, 0.0f, 1.0f);

    vec4 lightColor1 = mix(dark_violet, chartreuse, 0.5f);
    vec4 lightColor2 = vec4(1.0f, 1.0f, 1.0f, 1.0f);

    vec3 N = surf.wNorm; 

    vec4 color1 = max(dot(N, lightDir1) * 0.5 + 0.5, 0.0f) * lightColor1;
    vec4 color2 = max(dot(N, lightDir2) * 0.5 + 0.5, 0.0f) * lightColor2;
    vec4 color_lights = mix(color1, color2, 0.2f);

    out_fragColor = color_lights * Params.baseColor;
    out_fragColor = vec4(0);
    for (int i = 0; i < voxelsCount * 6; ++i)
    {
        for (int j = 0; j < 3; ++j)
        {
            out_fragColor += vec4(ff[voxelIdx * voxelsCount * 6 * 6 + i + j]) * max(0, N[j]);
            out_fragColor += vec4(ff[voxelIdx * voxelsCount * 6 * 6 + i + 3 + j]) * max(0, -N[j]);
        }
    }
}