#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require

#include "unpack_attributes.h"


layout(push_constant) uniform params_t
{
    mat4 mProjView;
    mat4 mModel;
} params;

layout(binding = 0, set = 0) buffer Points
{
    vec4 points[1000];
};

out gl_PerVertex { vec4 gl_Position; float gl_PointSize; };
void main(void)
{
    gl_Position   = params.mProjView * vec4((params.mModel * points[gl_VertexIndex]).xyz, 1.0);
    gl_PointSize = 2;
}
