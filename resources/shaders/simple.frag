#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_ray_query : require
#extension GL_EXT_nonuniform_qualifier : require
// #extension GL_EXT_debug_printf :enable
// #extension GL_EXT_spirv_intrinsics : require

#include "common.h"

layout(location = 0) out vec4 out_fragColor;

layout (location = 0 ) in VS_OUT
{
    vec3 wPos;
    vec3 wNorm;
    vec3 wTangent;
    vec2 texCoord;
    vec3 color;
    flat uint materialId;
} surf;

layout(binding = 0, set = 0) uniform AppData
{
    UniformParams Params;
};
layout(binding = 1, set = 0) buffer lighting_buf { vec4 lighting[]; };
layout(binding = 2, set = 0) buffer points_buf { uint points_cnt[]; };
layout(binding = 3, set = 0) buffer materialsBuf { MaterialData_pbrMR materials[]; };
layout(binding = 5, set = 0) uniform accelerationStructureEXT m_pAccelStruct;
layout(binding = 6, set = 0) uniform sampler2D textures[];


bool m_pAccelStruct_RayQuery_NearestHit(const vec3 rayPos, const vec3 rayDir, float len)
{
  rayQueryEXT rayQuery;
  rayQueryInitializeEXT(rayQuery, m_pAccelStruct, gl_RayFlagsOpaqueEXT, 0xff, rayPos.xyz, 0, rayDir.xyz, len);
  
  while(rayQueryProceedEXT(rayQuery)) { }
 

  return (rayQueryGetIntersectionTypeEXT(rayQuery, true) != gl_RayQueryCommittedIntersectionTriangleEXT);
}

float A = 0.15;
float B = 0.50;
float C = 0.10;
float D = 0.20;
float E = 0.02;
float F = 0.30;
float W = 11.2;

vec3 Uncharted2Tonemap(vec3 x)
{
     return ((x*(A*x+C*B)+D*E)/(x*(A*x+B)+D*F))-E/F;
}

vec4 postfx(vec3 texColor)
{
    texColor *= Params.exposureValue;
    float ExposureBias = 2.0f;
    vec3 curr = Uncharted2Tonemap(ExposureBias*texColor);
    vec3 whiteScale = 1.0f/Uncharted2Tonemap(vec3(W));
    vec3 color = curr*whiteScale;
    return vec4(color,1);
}

void main()
{
    vec3 coords = (surf.wPos - Params.bmin) / Params.voxelSize;
    uvec3 voxelCoord = uvec3(coords);
    uvec3 voxelsExtend = uvec3(ceil((Params.bmax - Params.bmin) / Params.voxelSize));
    uint voxelsCount = voxelsExtend.x * voxelsExtend.y * voxelsExtend.z;
    uint voxelIdx = (voxelCoord.x * voxelsExtend.y + voxelCoord.y) * voxelsExtend.z + voxelCoord.z;
    vec3 lightDir1 = normalize(Params.lightPos.xyz - surf.wPos);
    float traceDist = length(Params.lightPos.xyz - surf.wPos);
    // lightDir1 = vec3(0, 0.948773, 0.31596);
    // traceDist = 40.f;
    vec3 lightDir2 = vec3(0.0f, 0.0f, 1.0f);

    const vec4 dark_violet = vec4(0.59f, 0.0f, 0.82f, 1.0f);
    const vec4 chartreuse  = vec4(0.5f, 1.0f, 0.0f, 1.0f);

    vec4 lightColor1 = mix(dark_violet, chartreuse, 0.5f);
    vec4 lightColor2 = vec4(1.0f, 1.0f, 1.0f, 1.0f);

    vec3 N = surf.wNorm; 

    float color1 = max(dot(N, lightDir1), 0.0f);// * lightColor1;
    vec4 color2 = max(dot(N, lightDir2) * 0.5 + 0.5, 0.0f) * lightColor2;
    vec4 color_lights = vec4(color1) * vec4(1, 0.94902, 0.89803, 1) * 15.0f;//mix(color1, color2, 0.2f);
    if (color1 > 0.0f && !m_pAccelStruct_RayQuery_NearestHit(surf.wPos + N * 1e-3, lightDir1, traceDist))
        color_lights = vec4(0);

    vec3 UVW = (coords - voxelCoord) - 0.5;
    vec3 light[8];
    float weightSum = 0;
    if ((Params.interpolation & 1) == 1)
    {
        for (int i = 0; i < 2; ++i)
            for (int j = 0; j < 2; ++j)
                for (int k = 0; k < 2; ++k)
                {
                    vec3 voxLight = vec3(0);
                    ivec3 voxelId = ivec3(voxelCoord) + ivec3(i, j, k) * ivec3(sign(UVW));
                    uint voxelIdx = (voxelId.x * voxelsExtend.y + voxelId.y) * voxelsExtend.z + voxelId.z;
                    for (int z = 0; z < 3; ++z)
                    {
                        voxLight += lighting[voxelIdx * 6 + z].rgb * max(0, N[z]);
                        voxLight += lighting[voxelIdx * 6 + 3 + z].rgb * max(0, -N[z]);
                    }
                    float weight = 0;
                    if (points_cnt[4 * voxelIdx] > 0)
                    {
                        weight = 1;
                        weight *= i > 0 ? abs(UVW.x) : 1 - abs(UVW.x);
                        weight *= j > 0 ? abs(UVW.y) : 1 - abs(UVW.y);
                        weight *= k > 0 ? abs(UVW.z) : 1 - abs(UVW.z);
                    }
                    light[i * 4 + j * 2 + k] = voxLight * weight;
                    weightSum += weight;
                }
        for (int i = 1; i < 8; ++i)
            light[0] += light[i];
    }
    else
    {
        light[0] = vec3(0);
        for (int z = 0; z < 3; ++z)
        {
            light[0] += lighting[voxelIdx * 6 + z].rgb * max(0, N[z]);
            light[0] += lighting[voxelIdx * 6 + 3 + z].rgb * max(0, -N[z]);
        }
        weightSum = 1;
    }
    //138
    vec4 albedo = vec4(surf.color, 1);
    if (materials[uint(surf.materialId)].baseColorTexId != -1)
    {
        albedo = pow(texture(textures[materials[uint(surf.materialId)].baseColorTexId], surf.texCoord), vec4(2.2));
    }
    if (albedo.a < 0.5)
        discard;
    bool emission = materials[uint(surf.materialId)].emissionColor.x > 0;
    out_fragColor = vec4(0);
    if ((Params.interpolation & 2) == 2)
        out_fragColor += color_lights;
    if ((Params.interpolation & 4) == 4)
        out_fragColor += vec4(light[0] / weightSum, 1);
    if (!emission)
        out_fragColor *= albedo;
    //out_fragColor.xyz += materials[uint(surf.materialId)].emissionColor;
    if ((Params.interpolation & 8) == 8)
        out_fragColor = postfx(out_fragColor.rgb);
    out_fragColor = pow(out_fragColor, vec4(1 / 2.2));
    // float value = min(length(dFdx(surf.wPos)), length(dFdy(surf.wPos)));
    // if (value < 0.017)
    //     debugPrintfEXT("%f", value);
    // if (voxelIdx == 56)
    // {
    //     out_fragColor = vec4(1, 0, 0, 1);
    // }
    // out_fragColor.xyz = N;
}