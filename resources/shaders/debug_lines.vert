#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require
#extension GL_ARB_shader_draw_parameters  : enable

#include "unpack_attributes.h"


layout(push_constant) uniform params_t
{
    mat4 mProjView;
    uint perFacePointsCount;
} params;

layout(binding = 0, set = 0) buffer Points { vec4 points[]; };
layout(binding = 1, set = 0) buffer debugBuf { uint indices[]; };

layout (location = 0 ) out VS_OUT
{
    vec3 wNorm;
} vOut;

out gl_PerVertex { vec4 gl_Position; float gl_PointSize; };
void main(void)
{
    uint vertexId = indices[gl_VertexIndex];
    gl_Position   = params.mProjView * vec4(points[2 * vertexId].xyz, 1.0);
    vOut.wNorm = vec3(points[2 * vertexId + 1].w);
}
