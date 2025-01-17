#include <vector>
#include <array>
#include <memory>
#include <limits>

#include <cassert>
#include "vk_copy.h"
#include "vk_context.h"

#include "raytracing_generated.h"
#include "include/RayTracer_ubo.h"

static uint32_t ComputeReductionAuxBufferElements(uint32_t whole_size, uint32_t wg_size)
{
  uint32_t sizeTotal = 0;
  while (whole_size > 1)
  {
    whole_size  = (whole_size + wg_size - 1) / wg_size;
    sizeTotal  += std::max<uint32_t>(whole_size, 1);
  }
  return sizeTotal;
}

VkBufferUsageFlags RayTracer_Generated::GetAdditionalFlagsForUBO() const
{
  return 0;
}

RayTracer_Generated::~RayTracer_Generated()
{
  m_pMaker = nullptr;
  vkDestroyDescriptorSetLayout(device, CastSingleRayMegaDSLayout, nullptr);
  CastSingleRayMegaDSLayout = VK_NULL_HANDLE;

  vkDestroyPipeline(device, CastSingleRayMegaPipeline, nullptr);
  vkDestroyPipelineLayout(device, CastSingleRayMegaLayout, nullptr);
  CastSingleRayMegaLayout   = VK_NULL_HANDLE;
  CastSingleRayMegaPipeline = VK_NULL_HANDLE;
  vkDestroyDescriptorSetLayout(device, GenSamplesDSLayout, nullptr);
  GenSamplesDSLayout = VK_NULL_HANDLE;

  vkDestroyPipeline(device, ComputeFFPipeline, nullptr);
  vkDestroyPipelineLayout(device, ComputeFFLayout, nullptr);
  ComputeFFLayout   = VK_NULL_HANDLE;
  ComputeFFPipeline = VK_NULL_HANDLE;
  vkDestroyDescriptorSetLayout(device, ComputeFFDSLayout, nullptr);
  ComputeFFDSLayout = VK_NULL_HANDLE;

  vkDestroyPipeline(device, packFFPipeline, nullptr);
  vkDestroyPipelineLayout(device, packFFLayout, nullptr);
  packFFLayout   = VK_NULL_HANDLE;
  packFFPipeline = VK_NULL_HANDLE;
  vkDestroyDescriptorSetLayout(device, packFFDSLayout, nullptr);
  packFFDSLayout = VK_NULL_HANDLE;

  vkDestroyPipeline(device, initLightingPipeline, nullptr);
  vkDestroyPipelineLayout(device, initLightingLayout, nullptr);
  initLightingLayout   = VK_NULL_HANDLE;
  initLightingPipeline = VK_NULL_HANDLE;
  vkDestroyDescriptorSetLayout(device, initLightingDSLayout, nullptr);
  initLightingDSLayout = VK_NULL_HANDLE;

  vkDestroyPipeline(device, reflLightingPipeline, nullptr);
  vkDestroyPipelineLayout(device, reflLightingLayout, nullptr);
  reflLightingLayout   = VK_NULL_HANDLE;
  reflLightingPipeline = VK_NULL_HANDLE;
  vkDestroyDescriptorSetLayout(device, reflLightingDSLayout, nullptr);
  reflLightingDSLayout = VK_NULL_HANDLE;

  vkDestroyPipeline(device, aliasLightingPipeline, nullptr);
  vkDestroyPipelineLayout(device, aliasLightingLayout, nullptr);
  aliasLightingLayout   = VK_NULL_HANDLE;
  aliasLightingPipeline = VK_NULL_HANDLE;
  vkDestroyDescriptorSetLayout(device, aliasLightingDSLayout, nullptr);
  aliasLightingDSLayout = VK_NULL_HANDLE;
  
  vkDestroyPipeline(device, correctFFPipeline, nullptr);
  vkDestroyPipelineLayout(device, correctFFLayout, nullptr);
  correctFFLayout   = VK_NULL_HANDLE;
  correctFFPipeline = VK_NULL_HANDLE;
  vkDestroyDescriptorSetLayout(device, correctFFDSLayout, nullptr);
  correctFFDSLayout = VK_NULL_HANDLE;
  reflLightingDSLayout = VK_NULL_HANDLE;
  
  vkDestroyPipeline(device, finalLightingPipeline, nullptr);
  vkDestroyPipelineLayout(device, finalLightingLayout, nullptr);
  finalLightingLayout   = VK_NULL_HANDLE;
  finalLightingPipeline = VK_NULL_HANDLE;
  vkDestroyDescriptorSetLayout(device, finalLightingDSLayout, nullptr);
  finalLightingDSLayout = VK_NULL_HANDLE;

  vkDestroyPipeline(device, GenSamplesPipeline, nullptr);
  vkDestroyPipelineLayout(device, GenSamplesLayout, nullptr);
  GenSamplesLayout   = VK_NULL_HANDLE;
  GenSamplesPipeline = VK_NULL_HANDLE;
  vkDestroyDescriptorSetLayout(device, copyKernelFloatDSLayout, nullptr);
  vkDestroyDescriptorPool(device, m_dsPool, NULL); m_dsPool = VK_NULL_HANDLE;

  vkDestroyBuffer(device, CastSingleRay_local.rayDirAndFarBuffer, nullptr);
  vkDestroyBuffer(device, CastSingleRay_local.rayPosAndNearBuffer, nullptr);

 
  vkDestroyBuffer(device, m_classDataBuffer, nullptr);


  FreeAllAllocations(m_allMems);
}

void RayTracer_Generated::InitHelpers()
{
  vkGetPhysicalDeviceProperties(physicalDevice, &m_devProps);
  m_pMaker = std::make_unique<vk_utils::ComputePipelineMaker>();
}

VkDescriptorSetLayout RayTracer_Generated::CreateCastSingleRayMegaDSLayout()
{
  std::array<VkDescriptorSetLayoutBinding, 2+1> dsBindings;

  // binding for out_color
  dsBindings[0].binding            = 0;
  dsBindings[0].descriptorType     = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  dsBindings[0].descriptorCount    = 1;
  dsBindings[0].stageFlags         = VK_SHADER_STAGE_COMPUTE_BIT;
  dsBindings[0].pImmutableSamplers = nullptr;

  // binding for m_pAccelStruct
  dsBindings[1].binding            = 1;
  dsBindings[1].descriptorType     = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
  dsBindings[1].descriptorCount    = 1;
  dsBindings[1].stageFlags         = VK_SHADER_STAGE_COMPUTE_BIT;
  dsBindings[1].pImmutableSamplers = nullptr;

  // binding for POD members stored in m_classDataBuffer
  dsBindings[2].binding            = 2;
  dsBindings[2].descriptorType     = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  dsBindings[2].descriptorCount    = 1;
  dsBindings[2].stageFlags         = VK_SHADER_STAGE_COMPUTE_BIT;
  dsBindings[2].pImmutableSamplers = nullptr;
  
  VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo = {};
  descriptorSetLayoutCreateInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  descriptorSetLayoutCreateInfo.bindingCount = uint32_t(dsBindings.size());
  descriptorSetLayoutCreateInfo.pBindings    = dsBindings.data();
  
  VkDescriptorSetLayout layout = nullptr;
  VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCreateInfo, NULL, &layout));
  return layout;
}

VkDescriptorSetLayout RayTracer_Generated::GenSampleDSLayout()
{
  const uint32_t BUFFERS_COUNT = 12;
  std::array<VkDescriptorSetLayoutBinding, 2 + BUFFERS_COUNT> dsBindings;

  // binding for m_pAccelStruct
  dsBindings[0].binding            = 0;
  dsBindings[0].descriptorType     = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
  dsBindings[0].descriptorCount    = 1;
  dsBindings[0].stageFlags         = VK_SHADER_STAGE_COMPUTE_BIT;
  dsBindings[0].pImmutableSamplers = nullptr;

  for (uint32_t i = 0; i < BUFFERS_COUNT; ++i)
  {
    const uint32_t bindingId = i + 1;
    dsBindings[bindingId].binding            = bindingId;
    dsBindings[bindingId].descriptorType     = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    dsBindings[bindingId].descriptorCount    = 1;
    dsBindings[bindingId].stageFlags         = VK_SHADER_STAGE_COMPUTE_BIT;
    dsBindings[bindingId].pImmutableSamplers = nullptr;  
  }

  dsBindings.back().binding            = dsBindings.size() - 1;
  dsBindings.back().descriptorType     = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  dsBindings.back().descriptorCount    = 30;
  dsBindings.back().stageFlags         = VK_SHADER_STAGE_COMPUTE_BIT;
  dsBindings.back().pImmutableSamplers = nullptr;  
  
  VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo = {};
  descriptorSetLayoutCreateInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  descriptorSetLayoutCreateInfo.bindingCount = uint32_t(dsBindings.size());
  descriptorSetLayoutCreateInfo.pBindings    = dsBindings.data();
  
  VkDescriptorSetLayout layout = nullptr;
  VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCreateInfo, NULL, &layout));
  return layout;
}

VkDescriptorSetLayout RayTracer_Generated::CreateComputeFFDSLayout()
{
  const uint32_t BUFFERS_COUNT = 8;
  std::array<VkDescriptorSetLayoutBinding, 1 + BUFFERS_COUNT> dsBindings;

  // binding for m_pAccelStruct
  dsBindings[0].binding            = 0;
  dsBindings[0].descriptorType     = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
  dsBindings[0].descriptorCount    = 1;
  dsBindings[0].stageFlags         = VK_SHADER_STAGE_COMPUTE_BIT;
  dsBindings[0].pImmutableSamplers = nullptr;

  for (uint32_t i = 0; i < BUFFERS_COUNT; ++i)
  {
    const uint32_t bindingId = i + 1;
    dsBindings[bindingId].binding            = bindingId;
    dsBindings[bindingId].descriptorType     = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    dsBindings[bindingId].descriptorCount    = 1;
    dsBindings[bindingId].stageFlags         = VK_SHADER_STAGE_COMPUTE_BIT;
    dsBindings[bindingId].pImmutableSamplers = nullptr;  
  }
  
  VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo = {};
  descriptorSetLayoutCreateInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  descriptorSetLayoutCreateInfo.bindingCount = uint32_t(dsBindings.size());
  descriptorSetLayoutCreateInfo.pBindings    = dsBindings.data();
  
  VkDescriptorSetLayout layout = nullptr;
  VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCreateInfo, NULL, &layout));
  return layout;
}

VkDescriptorSetLayout RayTracer_Generated::CreatePackFFDSLayout()
{
  const uint32_t BUFFERS_COUNT = 3;
  std::array<VkDescriptorSetLayoutBinding, BUFFERS_COUNT> dsBindings;

  // binding for m_pAccelStruct
  dsBindings[0].binding            = 0;
  dsBindings[0].descriptorType     = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
  dsBindings[0].descriptorCount    = 1;
  dsBindings[0].stageFlags         = VK_SHADER_STAGE_COMPUTE_BIT;
  dsBindings[0].pImmutableSamplers = nullptr;

  for (uint32_t i = 0; i < BUFFERS_COUNT; ++i)
  {
    const uint32_t bindingId = i;
    dsBindings[bindingId].binding            = bindingId;
    dsBindings[bindingId].descriptorType     = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    dsBindings[bindingId].descriptorCount    = 1;
    dsBindings[bindingId].stageFlags         = VK_SHADER_STAGE_COMPUTE_BIT;
    dsBindings[bindingId].pImmutableSamplers = nullptr;  
  }
  
  VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo = {};
  descriptorSetLayoutCreateInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  descriptorSetLayoutCreateInfo.bindingCount = uint32_t(dsBindings.size());
  descriptorSetLayoutCreateInfo.pBindings    = dsBindings.data();
  
  VkDescriptorSetLayout layout = nullptr;
  VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCreateInfo, NULL, &layout));
  return layout;
}

VkDescriptorSetLayout RayTracer_Generated::CreateInitLightingDSLayout()
{
  const uint32_t BUFFERS_COUNT = 6;
  std::array<VkDescriptorSetLayoutBinding, 1 + BUFFERS_COUNT> dsBindings;

  // binding for m_pAccelStruct
  dsBindings[0].binding            = 0;
  dsBindings[0].descriptorType     = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
  dsBindings[0].descriptorCount    = 1;
  dsBindings[0].stageFlags         = VK_SHADER_STAGE_COMPUTE_BIT;
  dsBindings[0].pImmutableSamplers = nullptr;

  for (uint32_t i = 0; i < BUFFERS_COUNT; ++i)
  {
    const uint32_t bindingId = i + 1;
    dsBindings[bindingId].binding            = bindingId;
    dsBindings[bindingId].descriptorType     = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    dsBindings[bindingId].descriptorCount    = 1;
    dsBindings[bindingId].stageFlags         = VK_SHADER_STAGE_COMPUTE_BIT;
    dsBindings[bindingId].pImmutableSamplers = nullptr;  
  }
  
  VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo = {};
  descriptorSetLayoutCreateInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  descriptorSetLayoutCreateInfo.bindingCount = uint32_t(dsBindings.size());
  descriptorSetLayoutCreateInfo.pBindings    = dsBindings.data();
  
  VkDescriptorSetLayout layout = nullptr;
  VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCreateInfo, NULL, &layout));
  return layout;
}

VkDescriptorSetLayout RayTracer_Generated::CreateReflLightingDSLayout()
{
  const uint32_t BUFFERS_COUNT = 4;
  std::array<VkDescriptorSetLayoutBinding, BUFFERS_COUNT> dsBindings;

  for (uint32_t i = 0; i < BUFFERS_COUNT; ++i)
  {
    const uint32_t bindingId = i;
    dsBindings[bindingId].binding            = bindingId;
    dsBindings[bindingId].descriptorType     = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    dsBindings[bindingId].descriptorCount    = 1;
    dsBindings[bindingId].stageFlags         = VK_SHADER_STAGE_COMPUTE_BIT;
    dsBindings[bindingId].pImmutableSamplers = nullptr;  
  }
  
  VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo = {};
  descriptorSetLayoutCreateInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  descriptorSetLayoutCreateInfo.bindingCount = uint32_t(dsBindings.size());
  descriptorSetLayoutCreateInfo.pBindings    = dsBindings.data();
  
  VkDescriptorSetLayout layout = nullptr;
  VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCreateInfo, NULL, &layout));
  return layout;
}

VkDescriptorSetLayout RayTracer_Generated::CreateAliasLightingDSLayout()
{
  const uint32_t BUFFERS_COUNT = 4;
  std::array<VkDescriptorSetLayoutBinding, BUFFERS_COUNT> dsBindings;

  for (uint32_t i = 0; i < BUFFERS_COUNT; ++i)
  {
    const uint32_t bindingId = i;
    dsBindings[bindingId].binding            = bindingId;
    dsBindings[bindingId].descriptorType     = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    dsBindings[bindingId].descriptorCount    = 1;
    dsBindings[bindingId].stageFlags         = VK_SHADER_STAGE_COMPUTE_BIT;
    dsBindings[bindingId].pImmutableSamplers = nullptr;  
  }
  
  VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo = {};
  descriptorSetLayoutCreateInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  descriptorSetLayoutCreateInfo.bindingCount = uint32_t(dsBindings.size());
  descriptorSetLayoutCreateInfo.pBindings    = dsBindings.data();
  
  VkDescriptorSetLayout layout = nullptr;
  VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCreateInfo, NULL, &layout));
  return layout;
}

VkDescriptorSetLayout RayTracer_Generated::CreateCorrectFFDSLayout()
{
  const uint32_t BUFFERS_COUNT = 1;
  std::array<VkDescriptorSetLayoutBinding, BUFFERS_COUNT> dsBindings;

  for (uint32_t i = 0; i < BUFFERS_COUNT; ++i)
  {
    const uint32_t bindingId = i;
    dsBindings[bindingId].binding            = bindingId;
    dsBindings[bindingId].descriptorType     = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    dsBindings[bindingId].descriptorCount    = 1;
    dsBindings[bindingId].stageFlags         = VK_SHADER_STAGE_COMPUTE_BIT;
    dsBindings[bindingId].pImmutableSamplers = nullptr;  
  }
  
  VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo = {};
  descriptorSetLayoutCreateInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  descriptorSetLayoutCreateInfo.bindingCount = uint32_t(dsBindings.size());
  descriptorSetLayoutCreateInfo.pBindings    = dsBindings.data();
  
  VkDescriptorSetLayout layout = nullptr;
  VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCreateInfo, NULL, &layout));
  return layout;
}

VkDescriptorSetLayout RayTracer_Generated::CreateFinalLightingDSLayout()
{
  const uint32_t BUFFERS_COUNT = 3;
  std::array<VkDescriptorSetLayoutBinding, BUFFERS_COUNT> dsBindings;

  for (uint32_t i = 0; i < BUFFERS_COUNT; ++i)
  {
    const uint32_t bindingId = i;
    dsBindings[bindingId].binding            = bindingId;
    dsBindings[bindingId].descriptorType     = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    dsBindings[bindingId].descriptorCount    = 1;
    dsBindings[bindingId].stageFlags         = VK_SHADER_STAGE_COMPUTE_BIT;
    dsBindings[bindingId].pImmutableSamplers = nullptr;  
  }
  
  VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo = {};
  descriptorSetLayoutCreateInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  descriptorSetLayoutCreateInfo.bindingCount = uint32_t(dsBindings.size());
  descriptorSetLayoutCreateInfo.pBindings    = dsBindings.data();
  
  VkDescriptorSetLayout layout = nullptr;
  VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCreateInfo, NULL, &layout));
  return layout;
}

VkDescriptorSetLayout RayTracer_Generated::CreatecopyKernelFloatDSLayout()
{
  std::array<VkDescriptorSetLayoutBinding, 2> dsBindings;

  dsBindings[0].binding            = 0;
  dsBindings[0].descriptorType     = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  dsBindings[0].descriptorCount    = 1;
  dsBindings[0].stageFlags         = VK_SHADER_STAGE_COMPUTE_BIT;
  dsBindings[0].pImmutableSamplers = nullptr;

  dsBindings[1].binding            = 1;
  dsBindings[1].descriptorType     = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  dsBindings[1].descriptorCount    = 1;
  dsBindings[1].stageFlags         = VK_SHADER_STAGE_COMPUTE_BIT;
  dsBindings[1].pImmutableSamplers = nullptr;

  VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo = {};
  descriptorSetLayoutCreateInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  descriptorSetLayoutCreateInfo.bindingCount = dsBindings.size();
  descriptorSetLayoutCreateInfo.pBindings    = dsBindings.data();

  VkDescriptorSetLayout layout = nullptr;
  VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCreateInfo, NULL, &layout));
  return layout;
}


void RayTracer_Generated::InitKernel_CastSingleRayMega(const char* a_filePath)
{
  std::string shaderPath = AlterShaderPath("shaders_generated/CastSingleRayMega.comp.spv"); 
  
  m_pMaker->LoadShader(device, shaderPath.c_str(), nullptr, "main");
  CastSingleRayMegaDSLayout = CreateCastSingleRayMegaDSLayout();
  CastSingleRayMegaLayout   = m_pMaker->MakeLayout(device, { CastSingleRayMegaDSLayout }, 128); // at least 128 bytes for push constants
  CastSingleRayMegaPipeline = m_pMaker->MakePipeline(device);  

  shaderPath = AlterShaderPath("../../../resources/shaders/GenSamples.comp.spv");
  m_pMaker->LoadShader(device, shaderPath.c_str(), nullptr, "main");
  GenSamplesDSLayout = GenSampleDSLayout();
  GenSamplesLayout = m_pMaker->MakeLayout(device, { GenSamplesDSLayout }, 128);
  GenSamplesPipeline = m_pMaker->MakePipeline(device);

  shaderPath = AlterShaderPath("../../../resources/shaders/ComputeFF.comp.spv");
  m_pMaker->LoadShader(device, shaderPath.c_str(), nullptr, "main");
  ComputeFFDSLayout = CreateComputeFFDSLayout();
  ComputeFFLayout = m_pMaker->MakeLayout(device, { ComputeFFDSLayout }, 128);
  ComputeFFPipeline = m_pMaker->MakePipeline(device);

  shaderPath = AlterShaderPath("../../../resources/shaders/packFF.comp.spv");
  m_pMaker->LoadShader(device, shaderPath.c_str(), nullptr, "main");
  packFFDSLayout = CreatePackFFDSLayout();
  packFFLayout = m_pMaker->MakeLayout(device, { packFFDSLayout }, 128);
  packFFPipeline = m_pMaker->MakePipeline(device);

  shaderPath = AlterShaderPath("../../../resources/shaders/initLighting.comp.spv");
  m_pMaker->LoadShader(device, shaderPath.c_str(), nullptr, "main");
  initLightingDSLayout = CreateInitLightingDSLayout();
  initLightingLayout = m_pMaker->MakeLayout(device, { initLightingDSLayout }, 128);
  initLightingPipeline = m_pMaker->MakePipeline(device);
  
  shaderPath = AlterShaderPath("../../../resources/shaders/oneBounce.comp.spv");
  m_pMaker->LoadShader(device, shaderPath.c_str(), nullptr, "main");
  reflLightingDSLayout = CreateReflLightingDSLayout();
  reflLightingLayout = m_pMaker->MakeLayout(device, { reflLightingDSLayout }, 128);
  reflLightingPipeline = m_pMaker->MakePipeline(device);

  shaderPath = AlterShaderPath("../../../resources/shaders/aliasBounce.comp.spv");
  m_pMaker->LoadShader(device, shaderPath.c_str(), nullptr, "main");
  aliasLightingDSLayout = CreateAliasLightingDSLayout();
  aliasLightingLayout = m_pMaker->MakeLayout(device, { aliasLightingDSLayout }, 128);
  aliasLightingPipeline = m_pMaker->MakePipeline(device);

  shaderPath = AlterShaderPath("../../../resources/shaders/correctFF.comp.spv");
  m_pMaker->LoadShader(device, shaderPath.c_str(), nullptr, "main");
  correctFFDSLayout = CreateCorrectFFDSLayout();
  correctFFLayout = m_pMaker->MakeLayout(device, { correctFFDSLayout }, 128);
  correctFFPipeline = m_pMaker->MakePipeline(device);

  shaderPath = AlterShaderPath("../../../resources/shaders/FinalLighting.comp.spv");
  m_pMaker->LoadShader(device, shaderPath.c_str(), nullptr, "main");
  finalLightingDSLayout = CreateFinalLightingDSLayout();
  finalLightingLayout = m_pMaker->MakeLayout(device, { finalLightingDSLayout }, 128);
  finalLightingPipeline = m_pMaker->MakePipeline(device);
}


void RayTracer_Generated::InitKernels(const char* a_filePath)
{
  InitKernel_CastSingleRayMega(a_filePath);
}

void RayTracer_Generated::InitBuffers(size_t a_maxThreadsCount, bool a_tempBuffersOverlay)
{
  m_maxThreadCount = a_maxThreadsCount;
  std::vector<VkBuffer> allBuffers;
  allBuffers.reserve(64);

  struct BufferReqPair
  {
    BufferReqPair() {  }
    BufferReqPair(VkBuffer a_buff, VkDevice a_dev) : buf(a_buff) { vkGetBufferMemoryRequirements(a_dev, a_buff, &req); }
    VkBuffer             buf = VK_NULL_HANDLE;
    VkMemoryRequirements req = {};
  };

  struct LocalBuffers
  {
    std::vector<BufferReqPair> bufs;
    size_t                     size = 0;
    std::vector<VkBuffer>      bufsClean;
  };

  std::vector<LocalBuffers> groups;
  groups.reserve(16);

  LocalBuffers localBuffersCastSingleRay;
  localBuffersCastSingleRay.bufs.reserve(32);
  CastSingleRay_local.rayDirAndFarBuffer = vk_utils::createBuffer(device, sizeof(LiteMath::float4)*a_maxThreadsCount, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
  localBuffersCastSingleRay.bufs.push_back(BufferReqPair(CastSingleRay_local.rayDirAndFarBuffer, device));
  CastSingleRay_local.rayPosAndNearBuffer = vk_utils::createBuffer(device, sizeof(LiteMath::float4)*a_maxThreadsCount, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
  localBuffersCastSingleRay.bufs.push_back(BufferReqPair(CastSingleRay_local.rayPosAndNearBuffer, device));
  for(const auto& pair : localBuffersCastSingleRay.bufs)
  {
    allBuffers.push_back(pair.buf);
    localBuffersCastSingleRay.size += pair.req.size;
  }
  groups.push_back(localBuffersCastSingleRay);


  size_t largestIndex = 0;
  size_t largestSize  = 0;
  for(size_t i=0;i<groups.size();i++)
  {
    if(groups[i].size > largestSize)
    {
      largestIndex = i;
      largestSize  = groups[i].size;
    }
    groups[i].bufsClean.resize(groups[i].bufs.size());
    for(size_t j=0;j<groups[i].bufsClean.size();j++)
      groups[i].bufsClean[j] = groups[i].bufs[j].buf;
  }

  auto& allBuffersRef = a_tempBuffersOverlay ? groups[largestIndex].bufsClean : allBuffers;

  m_classDataBuffer = vk_utils::createBuffer(device, sizeof(m_uboData),  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | GetAdditionalFlagsForUBO());
  allBuffersRef.push_back(m_classDataBuffer);
  
  auto internalBuffersMem = AllocAndBind(allBuffersRef);
  if(a_tempBuffersOverlay)
  {
    for(size_t i=0;i<groups.size();i++)
      if(i != largestIndex)
        AssignBuffersToMemory(groups[i].bufsClean, internalBuffersMem.memObject);
  }
}

void RayTracer_Generated::InitMemberBuffers()
{
  std::vector<VkBuffer> memberVectors;
  std::vector<VkImage>  memberTextures;



  AllocMemoryForMemberBuffersAndImages(memberVectors, memberTextures);
}




void RayTracer_Generated::AssignBuffersToMemory(const std::vector<VkBuffer>& a_buffers, VkDeviceMemory a_mem)
{
  if(a_buffers.size() == 0 || a_mem == VK_NULL_HANDLE)
    return;

  std::vector<VkMemoryRequirements> memInfos(a_buffers.size());
  for(size_t i=0;i<memInfos.size();i++)
  {
    if(a_buffers[i] != VK_NULL_HANDLE)
      vkGetBufferMemoryRequirements(device, a_buffers[i], &memInfos[i]);
    else
    {
      memInfos[i] = memInfos[0];
      memInfos[i].size = 0;
    }
  }
  
  for(size_t i=1;i<memInfos.size();i++)
  {
    if(memInfos[i].memoryTypeBits != memInfos[0].memoryTypeBits)
    {
      std::cout << "[RayTracer_Generated::AssignBuffersToMemory]: error, input buffers has different 'memReq.memoryTypeBits'" << std::endl;
      return;
    }
  }

  auto offsets = vk_utils::calculateMemOffsets(memInfos);
  for (size_t i = 0; i < memInfos.size(); i++)
  {
    if(a_buffers[i] != VK_NULL_HANDLE)
      vkBindBufferMemory(device, a_buffers[i], a_mem, offsets[i]);
  }
}

RayTracer_Generated::MemLoc RayTracer_Generated::AllocAndBind(const std::vector<VkBuffer>& a_buffers)
{
  MemLoc currLoc;
  if(a_buffers.size() > 0)
  {
    currLoc.memObject = vk_utils::allocateAndBindWithPadding(device, physicalDevice, a_buffers);
    currLoc.allocId   = m_allMems.size();
    m_allMems.push_back(currLoc);
  }
  return currLoc;
}

RayTracer_Generated::MemLoc RayTracer_Generated::AllocAndBind(const std::vector<VkImage>& a_images)
{
  MemLoc currLoc;
  if(a_images.size() > 0)
  {
    std::vector<VkMemoryRequirements> reqs(a_images.size()); 
    for(size_t i=0; i<reqs.size(); i++)
      vkGetImageMemoryRequirements(device, a_images[i], &reqs[i]);

    for(size_t i=0; i<reqs.size(); i++)
    {
      if(reqs[i].memoryTypeBits != reqs[0].memoryTypeBits)
      {
        std::cout << "RayTracer_Generated::AllocAndBind(textures): memoryTypeBits warning, need to split mem allocation (override me)" << std::endl;
        break;
      }
    } 

    auto offsets  = vk_utils::calculateMemOffsets(reqs);
    auto memTotal = offsets[offsets.size() - 1];

    VkMemoryAllocateInfo allocateInfo = {};
    allocateInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.pNext           = nullptr;
    allocateInfo.allocationSize  = memTotal;
    allocateInfo.memoryTypeIndex = vk_utils::findMemoryType(reqs[0].memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, physicalDevice);
    VK_CHECK_RESULT(vkAllocateMemory(device, &allocateInfo, NULL, &currLoc.memObject));
    
    for(size_t i=0;i<a_images.size();i++) {
      VK_CHECK_RESULT(vkBindImageMemory(device, a_images[i], currLoc.memObject, offsets[i]));
    }

    currLoc.allocId = m_allMems.size();
    m_allMems.push_back(currLoc);
  }
  return currLoc;
}

void RayTracer_Generated::FreeAllAllocations(std::vector<MemLoc>& a_memLoc)
{
  // in general you may check 'mem.allocId' for unique to be sure you dont free mem twice
  // for default implementation this is not needed
  for(auto mem : a_memLoc)
    vkFreeMemory(device, mem.memObject, nullptr);
  a_memLoc.resize(0);
}     

void RayTracer_Generated::AllocMemoryForMemberBuffersAndImages(const std::vector<VkBuffer>& a_buffers, const std::vector<VkImage>& a_images)
{
  AllocAndBind(a_buffers);
}

