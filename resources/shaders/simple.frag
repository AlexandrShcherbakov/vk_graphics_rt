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
layout(binding = 1, set = 0) buffer lighting_buf { float lighting[]; };


void main()
{
    vec3 coords = (surf.wPos - Params.bmin) / Params.voxelSize;
    uvec3 voxelCoord = uvec3(coords);
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

    vec4 color1 = max(dot(N, lightDir1), 0.0f) * lightColor1;
    vec4 color2 = max(dot(N, lightDir2) * 0.5 + 0.5, 0.0f) * lightColor2;
    vec4 color_lights = color1;//mix(color1, color2, 0.2f);

    out_fragColor = color_lights * Params.baseColor;
    vec3 UVW = (coords - voxelCoord) - 0.5;
    float light[8];
    for (int i = 0; i < 2; ++i)
        for (int j = 0; j < 2; ++j)
            for (int k = 0; k < 2; ++k)
            {
                float voxLight = 0;
                ivec3 voxelId = ivec3(voxelCoord) + ivec3(i, j, k) * ivec3(sign(UVW));
                uint voxelIdx = (voxelId.x * voxelsExtend.y + voxelId.y) * voxelsExtend.z + voxelId.z;
                for (int z = 0; z < 3; ++z)
                {
                    voxLight += lighting[voxelIdx * 6 + z] * max(0, N[z]);
                    voxLight += lighting[voxelIdx * 6 + 3 + z] * max(0, -N[z]);
                }
                light[i * 4 + j * 2 + k] = voxLight;
            }
    for (int i = 0; i < 4; ++i)
    {
        light[i] = mix(light[i], light[i + 4], abs(UVW.x));
    }
    for (int i = 0; i < 2; ++i)
    {
        light[i] = mix(light[i], light[i + 2], abs(UVW.y));
    }
    light[0] = mix(light[0], light[1], abs(UVW.z));
    //138
    out_fragColor = vec4(light[0]);
    // if (voxelIdx == 33)
    // {
    //     out_fragColor = vec4(1, 0, 0, 1);
    // }
    // out_fragColor.xyz = N;
}