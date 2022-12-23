#ifndef MAIN_CLASS_DECL_RayTracer_H
#define MAIN_CLASS_DECL_RayTracer_H

#include <array>
#include <vector>
#include <memory>
#include <string>
#include <unordered_map>

#include "vk_pipeline.h"
#include "vk_buffers.h"
#include "vk_utils.h"
#include "vk_copy.h"
#include "vk_context.h"

#include "raytracing.h"

#include "include/RayTracer_ubo.h"

class RayTracer_Generated : public RayTracer
{
public:

  RayTracer_Generated(uint32_t a_width, uint32_t a_height) : RayTracer(a_width, a_height) {}
  virtual void InitVulkanObjects(VkDevice a_device, VkPhysicalDevice a_physicalDevice, size_t a_maxThreadsCount);
  virtual void SetVulkanContext(vk_utils::VulkanContext a_ctx) { m_ctx = a_ctx; }

  virtual void SetVulkanInOutFor_CastSingleRay(
    VkBuffer out_colorBuffer,
    size_t   out_colorOffset,
    uint32_t dummyArgument = 0)
  {
    CastSingleRay_local.out_colorBuffer = out_colorBuffer;
    CastSingleRay_local.out_colorOffset = out_colorOffset;
    InitAllGeneratedDescriptorSets_CastSingleRay();
  }

  virtual void SetVulkanInOutForGenSamples(
    VkBuffer points,
    VkBuffer indirect_buffer,
    VkBuffer out_points,
    VkBuffer vertex_buffer,
    VkBuffer index_buffer,
    VkBuffer matrices_buffer,
    VkBuffer inst_info_buffer,
    VkBuffer prim_counter_buffer,
    VkBuffer areas_buffer,
    VkBuffer ff_clustered_buffer,
    VkBuffer init_lighting_buffer,
    VkBuffer refl_buffer,
    VkBuffer debug_buffer,
    VkBuffer debug_indir_buffer)
  {
    genSamplesData.indirectBuffer = indirect_buffer;
    genSamplesData.inPointsBuffer = points;
    genSamplesData.outPointsBuffer = out_points;
    genSamplesData.vertexBuffer = vertex_buffer;
    genSamplesData.indexBuffer = index_buffer;
    genSamplesData.matricesBuffer = matrices_buffer;
    genSamplesData.instInfoBuffer = inst_info_buffer;
    genSamplesData.primCounterBuffer = prim_counter_buffer;
    genSamplesData.debugBuffer = debug_buffer;
    genSamplesData.debugIndirBuffer = debug_indir_buffer;
    ffData.areas = areas_buffer;
    ffData.clusteredBuffer = ff_clustered_buffer;
    lightingData.initialLighting = init_lighting_buffer;
    lightingData.reflLighting = refl_buffer;
    InitAllGeneratedDescriptorSets_GenSamples();
    InitAllGeneratedDescriptorSets_ComputeFF();
    InitAllGeneratedDescriptorSets_InitLighting();
    InitAllGeneratedDescriptorSets_ReflLighting();
    InitAllGeneratedDescriptorSets_CorrectFF();
  }

  virtual ~RayTracer_Generated();


  virtual void InitMemberBuffers();

  virtual void UpdateAll(std::shared_ptr<vk_utils::ICopyEngine> a_pCopyEngine)
  {
    UpdatePlainMembers(a_pCopyEngine);
    UpdateVectorMembers(a_pCopyEngine);
    UpdateTextureMembers(a_pCopyEngine);
  }
  
  
  virtual void UpdatePlainMembers(std::shared_ptr<vk_utils::ICopyEngine> a_pCopyEngine);
  virtual void UpdateVectorMembers(std::shared_ptr<vk_utils::ICopyEngine> a_pCopyEngine);
  virtual void UpdateTextureMembers(std::shared_ptr<vk_utils::ICopyEngine> a_pCopyEngine);
  
  virtual void CastSingleRayCmd(VkCommandBuffer a_commandBuffer, uint32_t tidX, uint32_t tidY, uint32_t* out_color);
  virtual void GenSamplesCmd(VkCommandBuffer a_commandBuffer, uint32_t points_per_voxel,
    LiteMath::float3 bmin,
    LiteMath::float3 bmax,
    float voxel_size,
    float time,
    LiteMath::float4x4 matrix);

  virtual void copyKernelFloatCmd(uint32_t length);
  
  virtual void CastSingleRayMegaCmd(uint32_t tidX, uint32_t tidY, uint32_t* out_color);
  void GenSamplesCmd(uint32_t points_per_voxel,
    LiteMath::float3 bmin,
    LiteMath::float3 bmax,
    float voxel_size,
    float time,
    LiteMath::float4x4 matrix);

  virtual void ComputeFFCmd(VkCommandBuffer a_commandBuffer, uint32_t points_per_voxel, uint32_t voxels_count);
  void ComputeFFCmd(uint32_t points_per_voxel, uint32_t voxels_count);
  void initLightingCmd(VkCommandBuffer a_commandBuffer,
    uint32_t voxels_count,
    float voxel_size,
    LiteMath::float3 bmin,
    LiteMath::float3 bmax,
    LiteMath::float3 light_pos);

  void reflLightingCmd(VkCommandBuffer a_commandBuffer, uint32_t voxels_count);
  void CorrectFFCmd(VkCommandBuffer a_commandBuffer, uint32_t voxels_count);
  
  struct MemLoc
  {
    VkDeviceMemory memObject = VK_NULL_HANDLE;
    size_t         memOffset = 0;
    size_t         allocId   = 0;
  };

  virtual MemLoc AllocAndBind(const std::vector<VkBuffer>& a_buffers); ///< replace this function to apply custom allocator
  virtual MemLoc AllocAndBind(const std::vector<VkImage>& a_image);    ///< replace this function to apply custom allocator
  virtual void   FreeAllAllocations(std::vector<MemLoc>& a_memLoc);    ///< replace this function to apply custom allocator

protected:

  VkPhysicalDevice        physicalDevice = VK_NULL_HANDLE;
  VkDevice                device         = VK_NULL_HANDLE;
  vk_utils::VulkanContext m_ctx          = {};

  VkCommandBuffer         m_currCmdBuffer   = VK_NULL_HANDLE;
  uint32_t                m_currThreadFlags = 0;

  std::vector<MemLoc>     m_allMems;

  std::unique_ptr<vk_utils::ComputePipelineMaker> m_pMaker = nullptr;
  VkPhysicalDeviceProperties m_devProps;

  VkBufferMemoryBarrier BarrierForClearFlags(VkBuffer a_buffer);
  VkBufferMemoryBarrier BarrierForSingleBuffer(VkBuffer a_buffer);
  void BarriersForSeveralBuffers(VkBuffer* a_inBuffers, VkBufferMemoryBarrier* a_outBarriers, uint32_t a_buffersNum);

  virtual void InitHelpers();
  virtual void InitBuffers(size_t a_maxThreadsCount, bool a_tempBuffersOverlay = true);
  virtual void InitKernels(const char* a_filePath);
  virtual void AllocateAllDescriptorSets();

  virtual void InitAllGeneratedDescriptorSets_CastSingleRay();
  virtual void InitAllGeneratedDescriptorSets_GenSamples();
  virtual void InitAllGeneratedDescriptorSets_ComputeFF();
  virtual void InitAllGeneratedDescriptorSets_InitLighting();
  virtual void InitAllGeneratedDescriptorSets_ReflLighting();
  virtual void InitAllGeneratedDescriptorSets_CorrectFF();

  virtual void AssignBuffersToMemory(const std::vector<VkBuffer>& a_buffers, VkDeviceMemory a_mem);

  virtual void AllocMemoryForMemberBuffersAndImages(const std::vector<VkBuffer>& a_buffers, const std::vector<VkImage>& a_image);
  virtual std::string AlterShaderPath(const char* in_shaderPath) { return std::string(in_shaderPath); }

  
  

  struct CastSingleRay_Data
  {
    VkBuffer rayDirAndFarBuffer = VK_NULL_HANDLE;
    size_t   rayDirAndFarOffset = 0;

    VkBuffer rayPosAndNearBuffer = VK_NULL_HANDLE;
    size_t   rayPosAndNearOffset = 0;

    VkBuffer out_colorBuffer = VK_NULL_HANDLE;
    size_t   out_colorOffset = 0;
  } CastSingleRay_local;

  struct GenSamplesData
  {
    VkBuffer inPointsBuffer = VK_NULL_HANDLE;
    VkBuffer outPointsBuffer = VK_NULL_HANDLE;
    VkBuffer indirectBuffer = VK_NULL_HANDLE;
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkBuffer indexBuffer = VK_NULL_HANDLE;
    VkBuffer matricesBuffer = VK_NULL_HANDLE;
    VkBuffer instInfoBuffer = VK_NULL_HANDLE;
    VkBuffer primCounterBuffer = VK_NULL_HANDLE;
    VkBuffer debugBuffer = VK_NULL_HANDLE;
    VkBuffer debugIndirBuffer = VK_NULL_HANDLE;
  } genSamplesData;

  struct FFData
  {
    VkBuffer areas = VK_NULL_HANDLE;
    VkBuffer clusteredBuffer = VK_NULL_HANDLE;
  } ffData;

  struct LightingData
  {
    VkBuffer initialLighting = VK_NULL_HANDLE;
    VkBuffer reflLighting = VK_NULL_HANDLE;
  } lightingData;

  struct MembersDataGPU
  {
  } m_vdata;

  size_t m_maxThreadCount = 0;
  VkBuffer m_classDataBuffer = VK_NULL_HANDLE;

  VkPipelineLayout      CastSingleRayMegaLayout   = VK_NULL_HANDLE;
  VkPipeline            CastSingleRayMegaPipeline = VK_NULL_HANDLE; 
  VkDescriptorSetLayout CastSingleRayMegaDSLayout = VK_NULL_HANDLE;
  VkDescriptorSetLayout CreateCastSingleRayMegaDSLayout();
  VkDescriptorSetLayout GenSampleDSLayout();
  VkDescriptorSetLayout CreateComputeFFDSLayout();
  VkDescriptorSetLayout CreateInitLightingDSLayout();
  VkDescriptorSetLayout CreateReflLightingDSLayout();
  VkDescriptorSetLayout CreateCorrectFFDSLayout();
  void InitKernel_CastSingleRayMega(const char* a_filePath);

  VkPipelineLayout      GenSamplesLayout   = VK_NULL_HANDLE;
  VkPipeline            GenSamplesPipeline = VK_NULL_HANDLE; 
  VkDescriptorSetLayout GenSamplesDSLayout = VK_NULL_HANDLE;

  VkPipelineLayout      ComputeFFLayout    = VK_NULL_HANDLE;
  VkPipeline            ComputeFFPipeline  = VK_NULL_HANDLE; 
  VkDescriptorSetLayout ComputeFFDSLayout  = VK_NULL_HANDLE;

  VkPipelineLayout      initLightingLayout    = VK_NULL_HANDLE;
  VkPipeline            initLightingPipeline  = VK_NULL_HANDLE; 
  VkDescriptorSetLayout initLightingDSLayout  = VK_NULL_HANDLE;

  VkPipelineLayout      reflLightingLayout    = VK_NULL_HANDLE;
  VkPipeline            reflLightingPipeline  = VK_NULL_HANDLE;
  VkDescriptorSetLayout reflLightingDSLayout  = VK_NULL_HANDLE;

  VkPipelineLayout      correctFFLayout    = VK_NULL_HANDLE;
  VkPipeline            correctFFPipeline  = VK_NULL_HANDLE;
  VkDescriptorSetLayout correctFFDSLayout  = VK_NULL_HANDLE;

  virtual VkBufferUsageFlags GetAdditionalFlagsForUBO() const;

  VkPipelineLayout      copyKernelFloatLayout   = VK_NULL_HANDLE;
  VkPipeline            copyKernelFloatPipeline = VK_NULL_HANDLE;
  VkDescriptorSetLayout copyKernelFloatDSLayout = VK_NULL_HANDLE;
  VkDescriptorSetLayout CreatecopyKernelFloatDSLayout();

  VkDescriptorPool m_dsPool = VK_NULL_HANDLE;
  std::array<VkDescriptorSet, 6>  m_allGeneratedDS;

  RayTracer_UBO_Data m_uboData;
  
  constexpr static uint32_t MEMCPY_BLOCK_SIZE = 256;
  constexpr static uint32_t REDUCTION_BLOCK_SIZE = 256;
};

#endif

