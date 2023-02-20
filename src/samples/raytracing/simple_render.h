#ifndef SIMPLE_RENDER_H
#define SIMPLE_RENDER_H

#define VK_NO_PROTOTYPES

#include "../../render/scene_mgr.h"
#include "../../render/render_common.h"
#include "../../render/render_gui.h"
#include "../../../resources/shaders/common.h"
#include <geom/vk_mesh.h>
#include <vk_descriptor_sets.h>
#include <vk_fbuf_attachment.h>
#include <vk_quad.h>
#include <vk_images.h>
#include <vk_swapchain.h>
#include <string>
#include <iostream>
#include <render/CrossRT.h>
#include "raytracing.h"
#include "raytracing_generated.h"

enum class RenderMode
{
  RASTERIZATION,
  RAYTRACING,
};

class RayTracer_GPU : public RayTracer_Generated
{
public:
  RayTracer_GPU(int32_t a_width, uint32_t a_height) : RayTracer_Generated(a_width, a_height) {} 
  std::string AlterShaderPath(const char* a_shaderPath) override { return std::string("../../src/samples/raytracing/") + std::string(a_shaderPath); }
};

class SimpleRender : public IRender
{
public:
  const std::string VERTEX_SHADER_PATH   = "../../resources/shaders/simple.vert";
  const std::string FRAGMENT_SHADER_PATH = "../../resources/shaders/simple.frag";

  static constexpr uint64_t STAGING_MEM_SIZE = 16 * 16 * 1024u;

  SimpleRender(uint32_t a_width, uint32_t a_height);
  ~SimpleRender()  { Cleanup(); };

  inline uint32_t     GetWidth()      const override { return m_width; }
  inline uint32_t     GetHeight()     const override { return m_height; }
  inline VkInstance   GetVkInstance() const override { return m_instance; }
  void InitVulkan(const char** a_instanceExtensions, uint32_t a_instanceExtensionsCount, uint32_t a_deviceId) override;

  void InitPresentation(VkSurfaceKHR& a_surface) override;

  void ProcessInput(const AppInput& input) override;
  void UpdateCamera(const Camera* cams, uint32_t a_camsCount) override;
  Camera GetCurrentCamera() override {return m_cam;}
  void UpdateView();

  void LoadScene(const char *path) override;
  void DrawFrame(float a_time, DrawMode a_mode) override;

  //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

  // debugging utils
  //
  static VKAPI_ATTR VkBool32 VKAPI_CALL debugReportCallbackFn(
    VkDebugReportFlagsEXT                       flags,
    VkDebugReportObjectTypeEXT                  objectType,
    uint64_t                                    object,
    size_t                                      location,
    int32_t                                     messageCode,
    const char* pLayerPrefix,
    const char* pMessage,
    void* pUserData)
  {
    (void)flags;
    (void)objectType;
    (void)object;
    (void)location;
    (void)messageCode;
    (void)pUserData;
    std::cout << pLayerPrefix << ": " << pMessage << std::endl;
    return VK_FALSE;
  }

  VkDebugReportCallbackEXT m_debugReportCallback = nullptr;
protected:

  VkInstance       m_instance       = VK_NULL_HANDLE;
  VkCommandPool    m_commandPool    = VK_NULL_HANDLE;
  VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
  VkDevice         m_device         = VK_NULL_HANDLE;
  VkQueue          m_graphicsQueue  = VK_NULL_HANDLE;
  VkQueue          m_transferQueue  = VK_NULL_HANDLE;

  std::shared_ptr<vk_utils::ICopyEngine> m_pCopyHelper;

  vk_utils::QueueFID_T m_queueFamilyIDXs {UINT32_MAX, UINT32_MAX, UINT32_MAX};

  RenderMode m_currentRenderMode = RenderMode::RASTERIZATION;

  struct
  {
    uint32_t    currentFrame      = 0u;
    VkQueue     queue             = VK_NULL_HANDLE;
    VkSemaphore imageAvailable    = VK_NULL_HANDLE;
    VkSemaphore renderingFinished = VK_NULL_HANDLE;
  } m_presentationResources;

  std::vector<VkFence> m_frameFences;
  std::vector<VkCommandBuffer> m_cmdBuffersDrawMain;

  struct
  {
    LiteMath::float4x4 projView;
    LiteMath::float4x4 model;
  } pushConst2M;

  UniformParams m_uniforms {};
  VkBuffer m_ubo = VK_NULL_HANDLE;
  VkDeviceMemory m_uboAlloc = VK_NULL_HANDLE;
  void* m_uboMappedMem = nullptr;

  std::shared_ptr<vk_utils::DescriptorMaker> m_pBindings = nullptr;

  pipeline_data_t m_basicForwardPipeline {};
  pipeline_data_t m_debugPointsPipeline {};
  pipeline_data_t m_debugLinesPipeline {};

  VkDescriptorSet m_dSet = VK_NULL_HANDLE;
  VkDescriptorSetLayout m_dSetLayout = VK_NULL_HANDLE;
  VkDescriptorSet pointsdSet = VK_NULL_HANDLE;
  VkDescriptorSetLayout pointsdSetLayout = VK_NULL_HANDLE;
  VkRenderPass m_screenRenderPass = VK_NULL_HANDLE; // rasterization renderpass

  LiteMath::float4x4 m_projectionMatrix;
  LiteMath::float4x4 m_inverseProjViewMatrix;

  // *** ray tracing
  // full screen quad resources to display ray traced image
  void GetRTFeatures();
  void * m_pDeviceFeatures;
  VkPhysicalDeviceAccelerationStructureFeaturesKHR m_accelStructFeatures{};
  VkPhysicalDeviceAccelerationStructureFeaturesKHR m_enabledAccelStructFeatures{};
  VkPhysicalDeviceBufferDeviceAddressFeatures m_enabledDeviceAddressFeatures{};
  VkPhysicalDeviceRayQueryFeaturesKHR m_enabledRayQueryFeatures;

  std::vector<uint32_t> m_raytracedImageData;
  std::shared_ptr<vk_utils::IQuad> m_pFSQuad;
  VkDescriptorSet m_quadDS = VK_NULL_HANDLE;
  VkDescriptorSetLayout m_quadDSLayout = VK_NULL_HANDLE;
  vk_utils::VulkanImageMem m_rtImage;
  VkSampler                m_rtImageSampler = VK_NULL_HANDLE;

  std::unique_ptr<RayTracer_GPU> m_pRayTracerGPU;
  void RayTraceGPU();
  void TraceGenSamples();

  VkBuffer m_genColorBuffer = VK_NULL_HANDLE;
  VkDeviceMemory m_colorMem = VK_NULL_HANDLE;
  //

  // *** presentation
  VkSurfaceKHR m_surface = VK_NULL_HANDLE;
  VulkanSwapChain m_swapchain;
  std::vector<VkFramebuffer> m_frameBuffers;
  vk_utils::VulkanImageMem m_depthBuffer{};
  // ***

  // *** GUI
  std::shared_ptr<IRenderGUI> m_pGUIRender;
  void SetupGUIElements();
  void DrawFrameWithGUI();
  //

  Camera   m_cam;
  uint32_t m_width  = 1024u;
  uint32_t m_height = 1024u;
  uint32_t m_framesInFlight  = 2u;
  bool m_vsync = false;

  VkPhysicalDeviceFeatures m_enabledDeviceFeatures = {};
  std::vector<const char*> m_deviceExtensions      = {};
  std::vector<const char*> m_instanceExtensions    = {};

  bool m_enableValidation;
  std::vector<const char*> m_validationLayers;

  std::shared_ptr<SceneManager> m_pScnMgr = nullptr;

  void DrawFrameSimple();

  void CreateInstance();
  void CreateDevice(uint32_t a_deviceId);

  void BuildCommandBufferSimple(VkCommandBuffer cmdBuff, VkFramebuffer frameBuff,
                                VkImageView a_targetImageView, VkPipeline a_pipeline);

  // *** Ray tracing related stuff
  void BuildCommandBufferQuad(VkCommandBuffer a_cmdBuff, VkImageView a_targetImageView);
  void SetupQuadRenderer();
  void SetupQuadDescriptors();
  void SetupRTImage();
  void SetupRTScene();
  // ***************************

  void SetupSimplePipeline();
  void CleanupPipelineAndSwapchain();
  void RecreateSwapChain();

  void CreateUniformBuffer();
  void UpdateUniformBuffer(float a_time);

  LiteMath::Box4f sceneBbox;

  void Cleanup();

  void SetupDeviceFeatures();
  void SetupDeviceExtensions();
  void SetupValidationLayers();
  void GetBbox();
  void setObjectName(VkBuffer buffer, const char *name);
  const uint32_t PER_SURFACE_POINTS = 16;
  const uint32_t PER_VOXEL_POINTS = PER_SURFACE_POINTS * 6;
  const uint32_t PER_VOXEL_CLUSTERS = 6;
  uint32_t pointsToDraw = 0;
  VkBuffer pointsBuffer = VK_NULL_HANDLE;
  VkDeviceMemory pointsMem = VK_NULL_HANDLE;
  VkBuffer indirectPointsBuffer = VK_NULL_HANDLE;
  VkDeviceMemory indirectPointsMem = VK_NULL_HANDLE;
  VkBuffer samplePointsBuffer = VK_NULL_HANDLE;
  VkDeviceMemory samplePointsMem = VK_NULL_HANDLE;
  VkBuffer primCounterBuffer = VK_NULL_HANDLE;
  VkDeviceMemory primCounterMem = VK_NULL_HANDLE;
  VkBuffer FFClusteredBuffer = VK_NULL_HANDLE;
  VkDeviceMemory FFClusteredMem = VK_NULL_HANDLE;
  VkBuffer initLightingBuffer = VK_NULL_HANDLE;
  VkDeviceMemory initLightingMem = VK_NULL_HANDLE;
  VkBuffer reflLightingBuffer = VK_NULL_HANDLE;
  VkDeviceMemory reflLightingMem = VK_NULL_HANDLE;
  VkBuffer debugBuffer = VK_NULL_HANDLE;
  VkDeviceMemory debugMem = VK_NULL_HANDLE;
  VkBuffer debugIndirBuffer = VK_NULL_HANDLE;
  VkDeviceMemory debugIndirMem = VK_NULL_HANDLE;
  VkBuffer indirVoxelsBuffer = VK_NULL_HANDLE;
  VkDeviceMemory indirVoxelsMem = VK_NULL_HANDLE;
  VkBuffer nonEmptyVoxelsBuffer = VK_NULL_HANDLE;
  VkDeviceMemory nonEmptyVoxelsMem = VK_NULL_HANDLE;
  VkBuffer appliedLightingBuffer = VK_NULL_HANDLE;
  VkDeviceMemory appliedLightingMem = VK_NULL_HANDLE;
  VkBuffer ffRowLenBuffer = VK_NULL_HANDLE;
  VkDeviceMemory ffRowLenMem = VK_NULL_HANDLE;
  VkBuffer ffTmpRowBuffer = VK_NULL_HANDLE;
  VkDeviceMemory ffTmpRowMem = VK_NULL_HANDLE;
  uint32_t trianglesCount = 0;
  const float VOXEL_SIZE = 0.125f;
  LiteMath::uint3 voxelsGrid;
  uint32_t voxelsCount = 0;
  uint32_t clustersCount = 0;
  uint32_t maxPointsCount = 0;
  uint32_t visibleVoxelsCount = 0;
  uint32_t visibleVoxelsApproxCount = 0;
  uint32_t approxColumns = 0;

  struct ComputeState
  {
    uint32_t ff_out = 0;
    uint32_t ff_in = 0;
    uint32_t version = 0;
  } computeState;
   const uint32_t FF_UPDATE_COUNT = 200000;
};


#endif //SIMPLE_RENDER_H
