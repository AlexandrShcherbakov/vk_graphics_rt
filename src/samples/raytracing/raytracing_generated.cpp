#include <vector>
#include <memory>
#include <limits>
#include <cassert>
#include <chrono>

#include "vk_copy.h"
#include "vk_context.h"
#include "vk_images.h"

#include "raytracing_generated.h"
#include "include/RayTracer_ubo.h"

#include "CrossRT.h"
ISceneObject* CreateVulkanRTX(VkDevice a_device, VkPhysicalDevice a_physDevice, uint32_t a_graphicsQId, std::shared_ptr<vk_utils::ICopyEngine> a_pCopyHelper,
                              uint32_t a_maxMeshes, uint32_t a_maxTotalVertices, uint32_t a_maxTotalPrimitives, uint32_t a_maxPrimitivesPerMesh,
                              bool build_as_add);

std::shared_ptr<RayTracer> CreateRayTracer_Generated(uint32_t a_width, uint32_t a_height, vk_utils::VulkanContext a_ctx, size_t a_maxThreadsGenerated) 
{ 
  auto pObj = std::make_shared<RayTracer_Generated>(a_width, a_height); 
  pObj->SetVulkanContext(a_ctx);
  pObj->InitVulkanObjects(a_ctx.device, a_ctx.physicalDevice, a_maxThreadsGenerated); 
  return pObj;
}

static uint32_t ComputeReductionSteps(uint32_t whole_size, uint32_t wg_size)
{
  uint32_t steps = 0;
  while (whole_size > 1)
  {
    steps++;
    whole_size = (whole_size + wg_size - 1) / wg_size;
  }
  return steps;
}

constexpr uint32_t KGEN_FLAG_RETURN            = 1;
constexpr uint32_t KGEN_FLAG_BREAK             = 2;
constexpr uint32_t KGEN_FLAG_DONT_SET_EXIT     = 4;
constexpr uint32_t KGEN_FLAG_SET_EXIT_NEGATIVE = 8;
constexpr uint32_t KGEN_REDUCTION_LAST_STEP    = 16;

void RayTracer_Generated::InitVulkanObjects(VkDevice a_device, VkPhysicalDevice a_physicalDevice, size_t a_maxThreadsCount) 
{
  physicalDevice = a_physicalDevice;
  device         = a_device;
  InitHelpers();
  InitBuffers(a_maxThreadsCount, true);
  InitKernels(".spv");
  AllocateAllDescriptorSets();

  auto queueAllFID = vk_utils::getQueueFamilyIndex(physicalDevice, VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_TRANSFER_BIT);
  //@TODO: calculate these somehow?
  uint32_t maxMeshes = 1024;
  uint32_t maxTotalVertices = 1'000'000;
  uint32_t maxTotalPrimitives = 1'000'000;
  uint32_t maxPrimitivesPerMesh = 200'000;
  m_pAccelStruct = std::shared_ptr<ISceneObject>(CreateVulkanRTX(a_device, a_physicalDevice, queueAllFID, m_ctx.pCopyHelper,
                                                             maxMeshes, maxTotalVertices, maxTotalPrimitives, maxPrimitivesPerMesh, true),
                                                            [](ISceneObject *p) { DeleteSceneRT(p); } );
}

void RayTracer_Generated::UpdatePlainMembers(std::shared_ptr<vk_utils::ICopyEngine> a_pCopyEngine)
{
  const size_t maxAllowedSize = std::numeric_limits<uint32_t>::max();

  m_uboData.m_invProjView = m_invProjView;
  m_uboData.m_camPos = m_camPos;
  m_uboData.m_height = m_height;
  m_uboData.m_width = m_width;
  a_pCopyEngine->UpdateBuffer(m_classDataBuffer, 0, &m_uboData, sizeof(m_uboData));
}


void RayTracer_Generated::UpdateVectorMembers(std::shared_ptr<vk_utils::ICopyEngine> a_pCopyEngine)
{
}

void RayTracer_Generated::UpdateTextureMembers(std::shared_ptr<vk_utils::ICopyEngine> a_pCopyEngine)
{ 
}

void RayTracer_Generated::CastSingleRayMegaCmd(uint32_t tidX, uint32_t tidY, uint32_t* out_color)
{
  uint32_t blockSizeX = 256;
  uint32_t blockSizeY = 1;
  uint32_t blockSizeZ = 1;

  struct KernelArgsPC
  {
    uint32_t m_sizeX;
    uint32_t m_sizeY;
    uint32_t m_sizeZ;
    uint32_t m_tFlags;
  } pcData;
  
  uint32_t sizeX  = uint32_t(tidX);
  uint32_t sizeY  = uint32_t(tidY);
  uint32_t sizeZ  = uint32_t(1);
  
  pcData.m_sizeX  = tidX;
  pcData.m_sizeY  = tidY;
  pcData.m_sizeZ  = 1;
  pcData.m_tFlags = m_currThreadFlags;

  vkCmdPushConstants(m_currCmdBuffer, CastSingleRayMegaLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(KernelArgsPC), &pcData);
  
  vkCmdBindPipeline(m_currCmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, CastSingleRayMegaPipeline);
  vkCmdDispatch    (m_currCmdBuffer, (sizeX + blockSizeX - 1) / blockSizeX, (sizeY + blockSizeY - 1) / blockSizeY, (sizeZ + blockSizeZ - 1) / blockSizeZ);
 
}

void RayTracer_Generated::GenSamplesCmd(uint32_t points_per_voxel,
  LiteMath::float3 bmin,
  LiteMath::float3 bmax,
  float voxel_size,
  float time,
  LiteMath::float4x4 matrix,
  uint32_t max_points_count)
{
  uint32_t blockSizeX = 256;

  struct KernelArgsPC
  {
    LiteMath::float3 bmin;
    uint32_t perFacePointsCount;
    LiteMath::float3 bmax;
    float voxelSize;
  } pcData;

  pcData.perFacePointsCount  = points_per_voxel;
  pcData.bmin = bmin;
  pcData.bmax = bmax;
  pcData.voxelSize = voxel_size;

  vkCmdPushConstants(m_currCmdBuffer, CastSingleRayMegaLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(KernelArgsPC), &pcData);

  vkCmdBindPipeline(m_currCmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, GenSamplesPipeline);
  vkCmdDispatch    (m_currCmdBuffer, (max_points_count + blockSizeX - 1) / blockSizeX, 1, 1);
}

void RayTracer_Generated::copyKernelFloatCmd(uint32_t length)
{
  uint32_t blockSizeX = MEMCPY_BLOCK_SIZE;

  vkCmdBindPipeline(m_currCmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, copyKernelFloatPipeline);
  vkCmdPushConstants(m_currCmdBuffer, copyKernelFloatLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t), &length);
  vkCmdDispatch(m_currCmdBuffer, (length + blockSizeX - 1) / blockSizeX, 1, 1);
}

VkBufferMemoryBarrier RayTracer_Generated::BarrierForClearFlags(VkBuffer a_buffer)
{
  VkBufferMemoryBarrier bar = {};
  bar.sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
  bar.pNext               = NULL;
  bar.srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
  bar.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
  bar.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  bar.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  bar.buffer              = a_buffer;
  bar.offset              = 0;
  bar.size                = VK_WHOLE_SIZE;
  return bar;
}

VkBufferMemoryBarrier RayTracer_Generated::BarrierForSingleBuffer(VkBuffer a_buffer)
{
  VkBufferMemoryBarrier bar = {};
  bar.sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
  bar.pNext               = NULL;
  bar.srcAccessMask       = VK_ACCESS_SHADER_WRITE_BIT;
  bar.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
  bar.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  bar.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  bar.buffer              = a_buffer;
  bar.offset              = 0;
  bar.size                = VK_WHOLE_SIZE;
  return bar;
}

void RayTracer_Generated::BarriersForSeveralBuffers(VkBuffer* a_inBuffers, VkBufferMemoryBarrier* a_outBarriers, uint32_t a_buffersNum)
{
  for(uint32_t i=0; i<a_buffersNum;i++)
  {
    a_outBarriers[i].sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    a_outBarriers[i].pNext               = NULL;
    a_outBarriers[i].srcAccessMask       = VK_ACCESS_SHADER_WRITE_BIT;
    a_outBarriers[i].dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
    a_outBarriers[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    a_outBarriers[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    a_outBarriers[i].buffer              = a_inBuffers[i];
    a_outBarriers[i].offset              = 0;
    a_outBarriers[i].size                = VK_WHOLE_SIZE;
  }
}

void RayTracer_Generated::CastSingleRayCmd(VkCommandBuffer a_commandBuffer, uint32_t tidX, uint32_t tidY, uint32_t* out_color)
{
  m_currCmdBuffer = a_commandBuffer;
  VkMemoryBarrier memoryBarrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT }; 
  vkCmdBindDescriptorSets(a_commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, CastSingleRayMegaLayout, 0, 1, &m_allGeneratedDS[0], 0, nullptr);
  CastSingleRayMegaCmd(tidX, tidY, out_color);
  vkCmdPipelineBarrier(m_currCmdBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &memoryBarrier, 0, nullptr, 0, nullptr); 
}

void RayTracer_Generated::GenSamplesCmd(VkCommandBuffer a_commandBuffer, uint32_t points_per_voxel,
  LiteMath::float3 bmin,
  LiteMath::float3 bmax,
  float voxel_size,
  float time,
  LiteMath::float4x4 matrix,
  uint32_t max_points_count)
{
  m_currCmdBuffer = a_commandBuffer;
  VkMemoryBarrier memoryBarrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT }; 
  vkCmdBindDescriptorSets(a_commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, GenSamplesLayout, 0, 1, &m_allGeneratedDS[1], 0, nullptr);
  GenSamplesCmd(points_per_voxel, bmin, bmax, voxel_size, time, matrix, max_points_count);
  vkCmdPipelineBarrier(m_currCmdBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &memoryBarrier, 0, nullptr, 0, nullptr); 
}

void RayTracer_Generated::ComputeFFCmd(VkCommandBuffer a_commandBuffer,
  uint32_t points_per_voxel, uint32_t voxels_count, uint32_t ff_out)
{
  m_currCmdBuffer = a_commandBuffer;
  VkMemoryBarrier memoryBarrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT }; 
  uint32_t blockSizeX = 256;

  struct KernelArgsPC
  {
    uint32_t perFacePointsCount;
    uint32_t voxelsCount;
    uint32_t ff_out;
  } pcData;

  pcData.perFacePointsCount  = points_per_voxel;
  pcData.voxelsCount = voxels_count;
  pcData.ff_out = ff_out;

  vkCmdBindDescriptorSets(a_commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, ComputeFFLayout, 0, 1, &m_allGeneratedDS[2], 0, nullptr);
  vkCmdPushConstants(m_currCmdBuffer, CastSingleRayMegaLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(KernelArgsPC), &pcData);
  vkCmdBindPipeline(m_currCmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, ComputeFFPipeline);
  vkCmdDispatch    (m_currCmdBuffer, voxels_count, 1, 1);
  vkCmdPipelineBarrier(m_currCmdBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &memoryBarrier, 0, nullptr, 0, nullptr); 
}

void RayTracer_Generated::packFFCmd(VkCommandBuffer a_commandBuffer, uint32_t points_per_voxel, uint32_t voxels_count, uint32_t ff_out)
{
  m_currCmdBuffer = a_commandBuffer;
  VkMemoryBarrier memoryBarrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT }; 
  uint32_t blockSizeX = 256;

  struct KernelArgsPC
  {
    uint32_t clustersCount;
    uint32_t rowIdx;
    uint32_t rowGlobalIdx;
  } pcData;

  pcData.clustersCount  = voxels_count * 6;

  vkCmdBindDescriptorSets(a_commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, packFFLayout, 0, 1, &m_allGeneratedDS[7], 0, nullptr);
  vkCmdBindPipeline(m_currCmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, packFFPipeline);
  for (uint32_t i = 0; i < 6; ++i)
  {
    pcData.rowIdx = i;
    pcData.rowGlobalIdx = ff_out * 6 + i;
    vkCmdPushConstants(m_currCmdBuffer, CastSingleRayMegaLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(KernelArgsPC), &pcData);
    vkCmdDispatch (m_currCmdBuffer, voxels_count * 6, 1, 1);
    VkBufferCopy region = {sizeof(uint32_t) * (ff_out * 6 + i + 1), sizeof(uint32_t) * (ff_out * 6 + i + 2), sizeof(uint32_t)};
    vkCmdCopyBuffer(m_currCmdBuffer, ffData.ffRowsLenBuffer, ffData.ffRowsLenBuffer, 1, &region);
  }
  vkCmdPipelineBarrier(m_currCmdBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &memoryBarrier, 0, nullptr, 0, nullptr); 
}

void RayTracer_Generated::initLightingCmd(VkCommandBuffer a_commandBuffer,
  uint32_t voxels_count,
  float voxel_size,
  LiteMath::float3 bmin,
  LiteMath::float3 bmax,
  LiteMath::float3 light_pos,
  uint32_t per_voxels_points_count,
  uint32_t multibounceFlag)
{
  m_currCmdBuffer = a_commandBuffer;
  VkMemoryBarrier memoryBarrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT }; 
  uint32_t blockSizeX = 256;

  struct KernelArgsPC
  {
    LiteMath::float3 bmin;
    uint32_t voxelsCount;
    LiteMath::float3 bmax;
    float voxelSize;
    LiteMath::float3 lightPos;
    uint32_t perVoxelsPointsCount;
    uint32_t multibounceFlag;
  } pcData;

  pcData.voxelsCount = voxels_count;
  pcData.bmin = bmin;
  pcData.bmax = bmax;
  pcData.voxelSize = voxel_size;
  pcData.lightPos = light_pos;
  pcData.perVoxelsPointsCount = per_voxels_points_count;
  pcData.multibounceFlag = multibounceFlag;

  vkCmdBindDescriptorSets(a_commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, initLightingLayout, 0, 1, &m_allGeneratedDS[3], 0, nullptr);
  vkCmdPushConstants(m_currCmdBuffer, CastSingleRayMegaLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(KernelArgsPC), &pcData);
  vkCmdBindPipeline(m_currCmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, initLightingPipeline);
  vkCmdDispatch    (m_currCmdBuffer, voxels_count, 1, 1);
  vkCmdPipelineBarrier(m_currCmdBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &memoryBarrier, 0, nullptr, 0, nullptr); 
}

void RayTracer_Generated::reflLightingCmd(VkCommandBuffer a_commandBuffer, uint32_t voxels_count)
{
  m_currCmdBuffer = a_commandBuffer;
  VkMemoryBarrier memoryBarrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT }; 
  uint32_t blockSizeX = 256;

  struct KernelArgsPC
  {
    uint32_t voxelsCount;
  } pcData;

  pcData.voxelsCount = voxels_count;

  vkCmdBindDescriptorSets(a_commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, reflLightingLayout, 0, 1, &m_allGeneratedDS[4], 0, nullptr);
  vkCmdPushConstants(m_currCmdBuffer, CastSingleRayMegaLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(KernelArgsPC), &pcData);
  vkCmdBindPipeline(m_currCmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, reflLightingPipeline);
  vkCmdDispatch    (m_currCmdBuffer, voxels_count * 6, 1, 1);
  vkCmdPipelineBarrier(m_currCmdBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &memoryBarrier, 0, nullptr, 0, nullptr); 
}

void RayTracer_Generated::aliasLightingCmd(VkCommandBuffer a_commandBuffer, uint32_t voxels_count)
{
  m_currCmdBuffer = a_commandBuffer;
  VkMemoryBarrier memoryBarrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT }; 
  uint32_t blockSizeX = 256;

  struct KernelArgsPC
  {
    uint32_t voxelsCount;
    uint32_t randValue;
    float threshold;
  } pcData;

  static int idx = 0;

  pcData.voxelsCount = voxels_count;
  pcData.randValue = rand();
  pcData.threshold = (float)rand() / RAND_MAX;
  idx++;

  vkCmdBindDescriptorSets(a_commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, aliasLightingLayout, 0, 1, &m_allGeneratedDS[8], 0, nullptr);
  vkCmdPushConstants(m_currCmdBuffer, CastSingleRayMegaLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(KernelArgsPC), &pcData);
  vkCmdBindPipeline(m_currCmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, aliasLightingPipeline);
  vkCmdDispatch    (m_currCmdBuffer, (voxels_count * 6 + blockSizeX - 1) / blockSizeX, 1, 1);
  vkCmdPipelineBarrier(m_currCmdBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &memoryBarrier, 0, nullptr, 0, nullptr); 
}

void RayTracer_Generated::CorrectFFCmd(VkCommandBuffer a_commandBuffer, uint32_t voxels_count)
{
  m_currCmdBuffer = a_commandBuffer;
  VkMemoryBarrier memoryBarrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT }; 
  uint32_t blockSizeX = 256;

  struct KernelArgsPC
  {
    uint32_t voxelsCount;
  } pcData;

  pcData.voxelsCount = voxels_count;

  vkCmdBindDescriptorSets(a_commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, correctFFLayout, 0, 1, &m_allGeneratedDS[5], 0, nullptr);
  vkCmdPushConstants(m_currCmdBuffer, CastSingleRayMegaLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(KernelArgsPC), &pcData);
  vkCmdBindPipeline(m_currCmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, correctFFPipeline);
  vkCmdDispatch    (m_currCmdBuffer, voxels_count, 1, 1);
  vkCmdPipelineBarrier(m_currCmdBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &memoryBarrier, 0, nullptr, 0, nullptr); 
}

void RayTracer_Generated::finalLightingCmd(VkCommandBuffer a_commandBuffer, uint32_t visible_voxels_count)
{
  m_currCmdBuffer = a_commandBuffer;
  VkMemoryBarrier memoryBarrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT }; 
  uint32_t blockSizeX = 256;

  struct KernelArgsPC
  {
    uint32_t visibleVoxelsCount;
  } pcData;

  pcData.visibleVoxelsCount = visible_voxels_count;

  vkCmdBindDescriptorSets(a_commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, finalLightingLayout, 0, 1, &m_allGeneratedDS[6], 0, nullptr);
  vkCmdPushConstants(m_currCmdBuffer, CastSingleRayMegaLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(KernelArgsPC), &pcData);
  vkCmdBindPipeline(m_currCmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, finalLightingPipeline);
  vkCmdDispatch    (m_currCmdBuffer, (visible_voxels_count + 255) / 256, 1, 1);
  vkCmdPipelineBarrier(m_currCmdBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &memoryBarrier, 0, nullptr, 0, nullptr); 
}
