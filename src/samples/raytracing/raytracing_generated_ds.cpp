#include <vector>
#include <array>
#include <memory>
#include <limits>

#include <cassert>
#include "vk_copy.h"
#include "vk_context.h"

#include "raytracing_generated.h"

#include "VulkanRTX.h"

void RayTracer_Generated::AllocateAllDescriptorSets()
{
  // allocate pool
  //
  VkDescriptorPoolSize buffersSize;
  buffersSize.type            = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  buffersSize.descriptorCount = 1*4 + 100; // mul 4 and add 100 because of AMD bug

  VkDescriptorPoolCreateInfo descriptorPoolCreateInfo = {};
  descriptorPoolCreateInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  descriptorPoolCreateInfo.maxSets       = 1 + 10; // add 1 to prevent zero case and one more for internal needs
  descriptorPoolCreateInfo.poolSizeCount = 1;
  descriptorPoolCreateInfo.pPoolSizes    = &buffersSize;
  
  VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolCreateInfo, NULL, &m_dsPool));
  
  // allocate all descriptor sets
  //
  VkDescriptorSetLayout layouts[4] = {};
  layouts[0] = CastSingleRayMegaDSLayout;
  layouts[1] = GenSamplesDSLayout;
  layouts[2] = ComputeFFDSLayout;
  layouts[3] = ClusterizeFFDSLayout;

  VkDescriptorSetAllocateInfo descriptorSetAllocateInfo = {};
  descriptorSetAllocateInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  descriptorSetAllocateInfo.descriptorPool     = m_dsPool;  
  descriptorSetAllocateInfo.descriptorSetCount = m_allGeneratedDS.size();
  descriptorSetAllocateInfo.pSetLayouts        = layouts;

  auto tmpRes = vkAllocateDescriptorSets(device, &descriptorSetAllocateInfo, m_allGeneratedDS.data());
  VK_CHECK_RESULT(tmpRes);
}

void RayTracer_Generated::InitAllGeneratedDescriptorSets_CastSingleRay()
{
  // now create actual bindings
  //
  // descriptor set #0: CastSingleRayMegaCmd (["out_color","m_pAccelStruct"])
  {
    constexpr uint additionalSize = 1;

    std::array<VkDescriptorBufferInfo, 2 + additionalSize> descriptorBufferInfo;
    std::array<VkDescriptorImageInfo,  2 + additionalSize> descriptorImageInfo;
    std::array<VkAccelerationStructureKHR,  2 + additionalSize> accelStructs;
    std::array<VkWriteDescriptorSetAccelerationStructureKHR,  2 + additionalSize> descriptorAccelInfo;
    std::array<VkWriteDescriptorSet,   2 + additionalSize> writeDescriptorSet;

    descriptorBufferInfo[0]        = VkDescriptorBufferInfo{};
    descriptorBufferInfo[0].buffer = CastSingleRay_local.out_colorBuffer;
    descriptorBufferInfo[0].offset = CastSingleRay_local.out_colorOffset;
    descriptorBufferInfo[0].range  = VK_WHOLE_SIZE;  

    writeDescriptorSet[0]                  = VkWriteDescriptorSet{};
    writeDescriptorSet[0].sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writeDescriptorSet[0].dstSet           = m_allGeneratedDS[0];
    writeDescriptorSet[0].dstBinding       = 0;
    writeDescriptorSet[0].descriptorCount  = 1;
    writeDescriptorSet[0].descriptorType   = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writeDescriptorSet[0].pBufferInfo      = &descriptorBufferInfo[0];
    writeDescriptorSet[0].pImageInfo       = nullptr;
    writeDescriptorSet[0].pTexelBufferView = nullptr; 

    {
      VulkanRTX* pScene = dynamic_cast<VulkanRTX*>(m_pAccelStruct.get());
      if(pScene == nullptr)
        std::cout << "[RayTracer_Generated::InitAllGeneratedDescriptorSets_CastSingleRay]: fatal error, wrong accel struct type" << std::endl;
      accelStructs       [1] = pScene->GetSceneAccelStruct();
      descriptorAccelInfo[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,VK_NULL_HANDLE,1,&accelStructs[1]};
    }

    writeDescriptorSet[1]                  = VkWriteDescriptorSet{};
    writeDescriptorSet[1].sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writeDescriptorSet[1].dstSet           = m_allGeneratedDS[0];
    writeDescriptorSet[1].dstBinding       = 1;
    writeDescriptorSet[1].descriptorCount  = 1;
    writeDescriptorSet[1].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    writeDescriptorSet[1].pNext          = &descriptorAccelInfo[1];

    descriptorBufferInfo[2]        = VkDescriptorBufferInfo{};
    descriptorBufferInfo[2].buffer = m_classDataBuffer;
    descriptorBufferInfo[2].offset = 0;
    descriptorBufferInfo[2].range  = VK_WHOLE_SIZE;  

    writeDescriptorSet[2]                  = VkWriteDescriptorSet{};
    writeDescriptorSet[2].sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writeDescriptorSet[2].dstSet           = m_allGeneratedDS[0];
    writeDescriptorSet[2].dstBinding       = 2;
    writeDescriptorSet[2].descriptorCount  = 1;
    writeDescriptorSet[2].descriptorType   = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writeDescriptorSet[2].pBufferInfo      = &descriptorBufferInfo[2];
    writeDescriptorSet[2].pImageInfo       = nullptr;
    writeDescriptorSet[2].pTexelBufferView = nullptr;

    vkUpdateDescriptorSets(device, uint32_t(writeDescriptorSet.size()), writeDescriptorSet.data(), 0, NULL);
  }
}


void RayTracer_Generated::InitAllGeneratedDescriptorSets_GenSamples()
{
  const uint32_t BUFFERS_COUNT = 8;
  std::array<VkDescriptorBufferInfo, BUFFERS_COUNT> descriptorBufferInfo;
  VkAccelerationStructureKHR accelStructs = {};
  VkWriteDescriptorSetAccelerationStructureKHR descriptorAccelInfo = {};
  std::array<VkWriteDescriptorSet, BUFFERS_COUNT + 1> writeDescriptorSet;

  {
    VulkanRTX* pScene = dynamic_cast<VulkanRTX*>(m_pAccelStruct.get());
    if(pScene == nullptr)
      std::cout << "[RayTracer_Generated::InitAllGeneratedDescriptorSets_CastSingleRay]: fatal error, wrong accel struct type" << std::endl;
    accelStructs = pScene->GetSceneAccelStruct();
    descriptorAccelInfo = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,VK_NULL_HANDLE,1, &accelStructs};
  }

  writeDescriptorSet[0]                  = VkWriteDescriptorSet{};
  writeDescriptorSet[0].sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  writeDescriptorSet[0].dstSet           = m_allGeneratedDS[1];
  writeDescriptorSet[0].dstBinding       = 0;
  writeDescriptorSet[0].descriptorCount  = 1;
  writeDescriptorSet[0].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
  writeDescriptorSet[0].pNext          = &descriptorAccelInfo;

  std::array<VkBuffer, BUFFERS_COUNT> buffersToBind = {
    genSamplesData.inPointsBuffer,
    genSamplesData.outPointsBuffer,
    genSamplesData.indirectBuffer,
    genSamplesData.vertexBuffer,
    genSamplesData.indexBuffer,
    genSamplesData.matricesBuffer,
    genSamplesData.instInfoBuffer,
    genSamplesData.primCounterBuffer
  };

  for (uint32_t i = 0; i < BUFFERS_COUNT; ++i)
  {
    descriptorBufferInfo[i]        = VkDescriptorBufferInfo{};
    descriptorBufferInfo[i].buffer = buffersToBind[i];
    descriptorBufferInfo[i].offset = 0;
    descriptorBufferInfo[i].range  = VK_WHOLE_SIZE;

    const uint32_t destBinding = i + 1;
    writeDescriptorSet[destBinding]                  = VkWriteDescriptorSet{};
    writeDescriptorSet[destBinding].sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writeDescriptorSet[destBinding].dstSet           = m_allGeneratedDS[1];
    writeDescriptorSet[destBinding].dstBinding       = destBinding;
    writeDescriptorSet[destBinding].descriptorCount  = 1;
    writeDescriptorSet[destBinding].descriptorType   = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writeDescriptorSet[destBinding].pBufferInfo      = &descriptorBufferInfo[i];
    writeDescriptorSet[destBinding].pImageInfo       = nullptr;
    writeDescriptorSet[destBinding].pTexelBufferView = nullptr; 
  }

  vkUpdateDescriptorSets(device, uint32_t(writeDescriptorSet.size()), writeDescriptorSet.data(), 0, NULL);
}

void RayTracer_Generated::InitAllGeneratedDescriptorSets_ComputeFF()
{
  const uint32_t BUFFERS_COUNT = 5;
  std::array<VkDescriptorBufferInfo, BUFFERS_COUNT> descriptorBufferInfo;
  VkAccelerationStructureKHR accelStructs = {};
  VkWriteDescriptorSetAccelerationStructureKHR descriptorAccelInfo = {};
  std::array<VkWriteDescriptorSet, 1 + BUFFERS_COUNT> writeDescriptorSet;
  {
    VulkanRTX* pScene = dynamic_cast<VulkanRTX*>(m_pAccelStruct.get());
    if(pScene == nullptr)
      std::cout << "[RayTracer_Generated::InitAllGeneratedDescriptorSets_CastSingleRay]: fatal error, wrong accel struct type" << std::endl;
    accelStructs = pScene->GetSceneAccelStruct();
    descriptorAccelInfo = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,VK_NULL_HANDLE,1, &accelStructs};
  }

  writeDescriptorSet[0]                  = VkWriteDescriptorSet{};
  writeDescriptorSet[0].sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  writeDescriptorSet[0].dstSet           = m_allGeneratedDS[2];
  writeDescriptorSet[0].dstBinding       = 0;
  writeDescriptorSet[0].descriptorCount  = 1;
  writeDescriptorSet[0].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
  writeDescriptorSet[0].pNext          = &descriptorAccelInfo;

  std::array<VkBuffer, descriptorBufferInfo.size()> buffers = {
    genSamplesData.outPointsBuffer,
    genSamplesData.indirectBuffer,
    genSamplesData.vertexBuffer,
    genSamplesData.primCounterBuffer,
    ffData.rawBuffer
  };

  for (uint32_t i = 0; i < descriptorBufferInfo.size(); ++i)
  {
    descriptorBufferInfo[i]        = VkDescriptorBufferInfo{};
    descriptorBufferInfo[i].buffer = buffers[i];
    descriptorBufferInfo[i].offset = 0;
    descriptorBufferInfo[i].range  = VK_WHOLE_SIZE;  

    writeDescriptorSet[i + 1]                  = VkWriteDescriptorSet{};
    writeDescriptorSet[i + 1].sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writeDescriptorSet[i + 1].dstSet           = m_allGeneratedDS[2];
    writeDescriptorSet[i + 1].dstBinding       = i + 1;
    writeDescriptorSet[i + 1].descriptorCount  = 1;
    writeDescriptorSet[i + 1].descriptorType   = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writeDescriptorSet[i + 1].pBufferInfo      = &descriptorBufferInfo[i];
    writeDescriptorSet[i + 1].pImageInfo       = nullptr;
    writeDescriptorSet[i + 1].pTexelBufferView = nullptr;
  }

  vkUpdateDescriptorSets(device, uint32_t(writeDescriptorSet.size()), writeDescriptorSet.data(), 0, NULL);
}

void RayTracer_Generated::InitAllGeneratedDescriptorSets_ClusterizeFF()
{
  const uint32_t BUFFERS_COUNT = 7;
  std::array<VkDescriptorBufferInfo, BUFFERS_COUNT> descriptorBufferInfo;
  std::array<VkWriteDescriptorSet, BUFFERS_COUNT> writeDescriptorSet;

  std::array<VkBuffer, descriptorBufferInfo.size()> buffers = {
    genSamplesData.outPointsBuffer,
    genSamplesData.indirectBuffer,
    genSamplesData.vertexBuffer,
    genSamplesData.primCounterBuffer,
    ffData.rawBuffer,
    ffData.areas,
    ffData.clusteredBuffer
  };

  for (uint32_t i = 0; i < descriptorBufferInfo.size(); ++i)
  {
    descriptorBufferInfo[i]        = VkDescriptorBufferInfo{};
    descriptorBufferInfo[i].buffer = buffers[i];
    descriptorBufferInfo[i].offset = 0;
    descriptorBufferInfo[i].range  = VK_WHOLE_SIZE;  

    writeDescriptorSet[i]                  = VkWriteDescriptorSet{};
    writeDescriptorSet[i].sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writeDescriptorSet[i].dstSet           = m_allGeneratedDS[3];
    writeDescriptorSet[i].dstBinding       = i;
    writeDescriptorSet[i].descriptorCount  = 1;
    writeDescriptorSet[i].descriptorType   = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writeDescriptorSet[i].pBufferInfo      = &descriptorBufferInfo[i];
    writeDescriptorSet[i].pImageInfo       = nullptr;
    writeDescriptorSet[i].pTexelBufferView = nullptr;
  }

  vkUpdateDescriptorSets(device, uint32_t(writeDescriptorSet.size()), writeDescriptorSet.data(), 0, NULL);
}
