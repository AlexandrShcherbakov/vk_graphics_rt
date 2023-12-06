#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require
#extension GL_ARB_shader_draw_parameters  : enable

#include "unpack_attributes.h"

struct SamplePoint
{
  vec4 position;
  vec4 normal;
  vec4 color;
};

layout(binding = 0, set = 0) buffer counters { uvec4 indirect_buf[]; };
layout(binding = 1, set = 0) buffer point_buf { SamplePoint points[]; };


layout(push_constant) uniform params_t
{
    mat4 mProjView;
    vec3 bmin;
    uint voxelsCount;
    vec3 bmax;
    float voxelSize;
    uint maxPointsPerVoxelCount;
    float debugCubesScale;
} params;

layout (location = 0 ) out VS_OUT
{
    vec3 wNorm;
} vOut;

void main(void)
{
    uint vertexId = gl_VertexIndex;
    uint side = vertexId / 6;
    vec3 normals[6] = 
    {
        vec3(1, 0, 0),
        vec3(0, 1, 0),
        vec3(0, 0, 1),
        vec3(-1, 0, 0),
        vec3(0, -1, 0),
        vec3(0, 0, -1)
    };
    uint tri = vertexId % 6 / 3;
    uint vert = vertexId % 3;
    vec3 side1 = normals[(side + 1) % 6];
    vec3 side2 = normals[(side + 2) % 6];
    vec3 pos = normals[side];
    if (tri == 1)
    {
        pos += (vert & 1) == 0 ? -side1 : side1;
        pos += (vert & 2) == 0 ? -side2 : side2;
    }
    else
    {
        pos += (vert & 1) == 0 ? side1 : -side1;
        pos += (vert & 2) == 0 ? side2 : -side2;
    }
    pos *= params.voxelSize * params.debugCubesScale * 0.5;
    uvec3 voxelsExtend = uvec3(ceil((params.bmax - params.bmin) / params.voxelSize));
    uint voxelId = gl_InstanceIndex;
    uint zVoxel = voxelId % voxelsExtend.z;
    voxelId /= voxelsExtend.z;
    uint yVoxel = voxelId % voxelsExtend.y;
    voxelId /= voxelsExtend.y;
    uint xVoxel = voxelId % voxelsExtend.x;
    voxelId /= voxelsExtend.x;
    vec3 offset = params.bmin + (vec3(xVoxel, yVoxel, zVoxel) + 0.5) * params.voxelSize;
    gl_Position   = params.mProjView * (vec4(pos + offset, 1));
    vOut.wNorm = vec3(0);
    for (int i = 0; i < indirect_buf[gl_InstanceIndex].x; ++i)
    {
        uint color = floatBitsToUint(points[params.maxPointsPerVoxelCount * gl_InstanceIndex + i].color.x);
        float mult = points[params.maxPointsPerVoxelCount * gl_InstanceIndex + i].color.z;
        vOut.wNorm += (vec3(uvec3((color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF)) / 255.0 * mult)
            ;//* max(dot(normals[side], points[params.maxPointsPerVoxelCount * gl_InstanceIndex + i].normal.xyz), 0);
    }
    if (indirect_buf[gl_InstanceIndex].x == 0)
    {
        gl_Position = vec4(-5, -5, 0, 1);
    }
    else
        vOut.wNorm /= indirect_buf[gl_InstanceIndex].x;
}
