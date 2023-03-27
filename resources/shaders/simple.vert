#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require
#extension GL_ARB_shader_draw_parameters  : enable

#include "common.h"
#include "unpack_attributes.h"


layout(location = 0) in vec4 vPosNorm;
layout(location = 1) in vec4 vTexCoordAndTang;

layout(push_constant) uniform params_t
{
    mat4 mProjView;
    mat4 mModel;
} params;


layout (location = 0 ) out VS_OUT
{
    vec3 wPos;
    vec3 wNorm;
    vec3 wTangent;
    vec2 texCoord;
    vec3 color;
    flat uint materialId;
} vOut;

layout(binding = 3, set = 0) buffer materialsBuf { MaterialData_pbrMR materials[]; };
layout(binding = 4, set = 0) buffer materialIdsBuf { uint materialIds[]; };
layout(binding = 7, set = 0) buffer perInstInfo { uvec2 instInfo[]; };

out gl_PerVertex { vec4 gl_Position; };
void main(void)
{
    const vec4 wNorm = vec4(DecodeNormal(floatBitsToInt(vPosNorm.w)),         0.0f);
    const vec4 wTang = vec4(DecodeNormal(floatBitsToInt(vTexCoordAndTang.z)), 0.0f);

    vOut.wPos     = (params.mModel * vec4(vPosNorm.xyz, 1.0f)).xyz;
    vOut.wNorm    = normalize(mat3(transpose(inverse(params.mModel))) * wNorm.xyz);
    vOut.wTangent = normalize(mat3(transpose(inverse(params.mModel))) * wTang.xyz);
    vOut.texCoord = vTexCoordAndTang.xy;
    vOut.color = materials[materialIds[gl_BaseVertexARB]].baseColor.xyz;
    vOut.materialId = materialIds[gl_BaseVertexARB];

    gl_Position   = params.mProjView * vec4(vOut.wPos, 1.0);
}
