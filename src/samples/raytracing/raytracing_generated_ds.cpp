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
  descriptorPoolCreateInfo.maxSets       = 1 + 2; // add 1 to prevent zero case and one more for internal needs
  descriptorPoolCreateInfo.poolSizeCount = 1;
  descriptorPoolCreateInfo.pPoolSizes    = &buffersSize;
  
  VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolCreateInfo, NULL, &m_dsPool));
  
  // allocate all descriptor sets
  //
  VkDescriptorSetLayout layouts[2] = {};
  layouts[0] = CastSingleRayMegaDSLayout;
  layouts[1] = GenSamplesDSLayout;

  VkDescriptorSetAllocateInfo descriptorSetAllocateInfo = {};
  descriptorSetAllocateInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  descriptorSetAllocateInfo.descriptorPool     = m_dsPool;  
  descriptorSetAllocateInfo.descriptorSetCount = 2;     
  descriptorSetAllocateInfo.pSetLayouts        = layouts;

  auto tmpRes = vkAllocateDescriptorSets(device, &descriptorSetAllocateInfo, m_allGeneratedDS);
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
  std::array<VkDescriptorBufferInfo, 3> descriptorBufferInfo;
  VkAccelerationStructureKHR accelStructs = {};
  VkWriteDescriptorSetAccelerationStructureKHR descriptorAccelInfo = {};
  std::array<VkWriteDescriptorSet, 4> writeDescriptorSet;

  descriptorBufferInfo[0]        = VkDescriptorBufferInfo{};
  descriptorBufferInfo[0].buffer = genSamplesData.inPointsBuffer;
  descriptorBufferInfo[0].offset = 0;
  descriptorBufferInfo[0].range  = VK_WHOLE_SIZE;  

  writeDescriptorSet[0]                  = VkWriteDescriptorSet{};
  writeDescriptorSet[0].sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  writeDescriptorSet[0].dstSet           = m_allGeneratedDS[1];
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
    accelStructs = pScene->GetSceneAccelStruct();
    descriptorAccelInfo = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,VK_NULL_HANDLE,1, &accelStructs};
  }

  writeDescriptorSet[1]                  = VkWriteDescriptorSet{};
  writeDescriptorSet[1].sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  writeDescriptorSet[1].dstSet           = m_allGeneratedDS[1];
  writeDescriptorSet[1].dstBinding       = 1;
  writeDescriptorSet[1].descriptorCount  = 1;
  writeDescriptorSet[1].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
  writeDescriptorSet[1].pNext          = &descriptorAccelInfo;

  descriptorBufferInfo[1]        = VkDescriptorBufferInfo{};
  descriptorBufferInfo[1].buffer = genSamplesData.outPointsBuffer;
  descriptorBufferInfo[1].offset = 0;
  descriptorBufferInfo[1].range  = VK_WHOLE_SIZE;  

  writeDescriptorSet[2]                  = VkWriteDescriptorSet{};
  writeDescriptorSet[2].sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  writeDescriptorSet[2].dstSet           = m_allGeneratedDS[1];
  writeDescriptorSet[2].dstBinding       = 2;
  writeDescriptorSet[2].descriptorCount  = 1;
  writeDescriptorSet[2].descriptorType   = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  writeDescriptorSet[2].pBufferInfo      = &descriptorBufferInfo[1];
  writeDescriptorSet[2].pImageInfo       = nullptr;
  writeDescriptorSet[2].pTexelBufferView = nullptr;

  descriptorBufferInfo[2]        = VkDescriptorBufferInfo{};
  descriptorBufferInfo[2].buffer = genSamplesData.indirectBuffer;
  descriptorBufferInfo[2].offset = 0;
  descriptorBufferInfo[2].range  = VK_WHOLE_SIZE;  

  writeDescriptorSet[3]                  = VkWriteDescriptorSet{};
  writeDescriptorSet[3].sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  writeDescriptorSet[3].dstSet           = m_allGeneratedDS[1];
  writeDescriptorSet[3].dstBinding       = 3;
  writeDescriptorSet[3].descriptorCount  = 1;
  writeDescriptorSet[3].descriptorType   = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  writeDescriptorSet[3].pBufferInfo      = &descriptorBufferInfo[2];
  writeDescriptorSet[3].pImageInfo       = nullptr;
  writeDescriptorSet[3].pTexelBufferView = nullptr;

  vkUpdateDescriptorSets(device, uint32_t(writeDescriptorSet.size()), writeDescriptorSet.data(), 0, NULL);
}
