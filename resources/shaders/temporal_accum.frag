#version 460
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) out vec4 out_fragColor;

layout (location = 0 ) in VS_OUT
{
    vec2 texCoord;
} surf;

layout(binding = 0, set = 0) uniform sampler2D frame;
layout(binding = 1, set = 0) uniform sampler2D previousFrame;

layout( push_constant ) uniform kernelArgs
{
  float blendFactor;
} kgenArgs;

void main()
{   
    out_fragColor = mix(
      texture(frame, surf.texCoord),
      texture(previousFrame, surf.texCoord),
      vec4(kgenArgs.blendFactor));
}