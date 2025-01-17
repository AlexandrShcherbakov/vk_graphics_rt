#include "simple_render.h"
#include "../../utils/input_definitions.h"

#include <geom/vk_mesh.h>
#include <vk_pipeline.h>
#include <vk_buffers.h>

#include <random>
#include <chrono>
#include "stb_image_write.h"

SimpleRender::SimpleRender(uint32_t a_width, uint32_t a_height) : m_width(a_width), m_height(a_height)
{
#ifdef NDEBUG
  m_enableValidation = false;
#else
  m_enableValidation = true;
#endif

  m_raytracedImageData.resize(m_width * m_height);
}

void SimpleRender::SetupDeviceFeatures()
{
    // m_enabledDeviceFeatures.fillModeNonSolid = VK_TRUE;
    m_enabledRayQueryFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
    m_enabledRayQueryFeatures.rayQuery = VK_TRUE;
    m_enabledRayQueryFeatures.pNext = nullptr;
    
    m_enabledDeviceAddressFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
    m_enabledDeviceAddressFeatures.bufferDeviceAddress = VK_TRUE;
    m_enabledDeviceAddressFeatures.pNext = &m_enabledRayQueryFeatures;
    
    m_enabledAccelStructFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
    m_enabledAccelStructFeatures.accelerationStructure = VK_TRUE;
    m_enabledAccelStructFeatures.pNext = &m_enabledDeviceAddressFeatures;
    
    m_pDeviceFeatures = &m_enabledAccelStructFeatures;
    
}

void SimpleRender::SetupDeviceExtensions()
{
  m_deviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
  
  m_deviceExtensions.push_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
  m_deviceExtensions.push_back(VK_KHR_RAY_QUERY_EXTENSION_NAME);
  // Required by VK_KHR_acceleration_structure
  m_deviceExtensions.push_back(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);
  m_deviceExtensions.push_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
  m_deviceExtensions.push_back(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME);
  // Required by VK_KHR_ray_tracing_pipeline
  m_deviceExtensions.push_back(VK_KHR_SPIRV_1_4_EXTENSION_NAME);
  // Required by VK_KHR_spirv_1_4
  m_deviceExtensions.push_back(VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME);

  m_deviceExtensions.push_back(VK_KHR_SHADER_NON_SEMANTIC_INFO_EXTENSION_NAME);
}

void SimpleRender::GetRTFeatures()
{
  m_accelStructFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;

  VkPhysicalDeviceFeatures2 deviceFeatures2{};
  deviceFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
  deviceFeatures2.pNext = &m_accelStructFeatures;
  vkGetPhysicalDeviceFeatures2(m_physicalDevice, &deviceFeatures2);
}

void SimpleRender::SetupValidationLayers()
{
  m_validationLayers.push_back("VK_LAYER_KHRONOS_validation");
  m_validationLayers.push_back("VK_LAYER_LUNARG_monitor");
}

void SimpleRender::InitVulkan(const char** a_instanceExtensions, uint32_t a_instanceExtensionsCount, uint32_t a_deviceId)
{
  for(size_t i = 0; i < a_instanceExtensionsCount; ++i)
  {
    m_instanceExtensions.push_back(a_instanceExtensions[i]);
  }

  SetupValidationLayers();
  VK_CHECK_RESULT(volkInitialize());
  CreateInstance();
  volkLoadInstance(m_instance);

  CreateDevice(a_deviceId);
  volkLoadDevice(m_device);

  GetRTFeatures();

  m_commandPool = vk_utils::createCommandPool(m_device, m_queueFamilyIDXs.graphics,
                                              VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

  m_cmdBuffersDrawMain.reserve(m_framesInFlight);
  m_cmdBuffersDrawMain = vk_utils::createCommandBuffers(m_device, m_commandPool, m_framesInFlight);

  m_frameFences.resize(m_framesInFlight);
  VkFenceCreateInfo fenceInfo = {};
  fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
  for (size_t i = 0; i < m_framesInFlight; i++)
  {
    VK_CHECK_RESULT(vkCreateFence(m_device, &fenceInfo, nullptr, &m_frameFences[i]));
  }

  m_pCopyHelper = std::make_shared<vk_utils::PingPongCopyHelper>(m_physicalDevice, m_device, m_transferQueue,
    m_queueFamilyIDXs.transfer, STAGING_MEM_SIZE);

  LoaderConfig conf = {};
  conf.load_geometry = true;
  conf.load_materials = MATERIAL_LOAD_MODE::MATERIALS_AND_TEXTURES;
  conf.build_acc_structs = true;
  conf.build_acc_structs_while_loading_scene = true;
  conf.builder_type = BVH_BUILDER_TYPE::RTX;

  m_pScnMgr = std::make_shared<SceneManager>(m_device, m_physicalDevice, m_queueFamilyIDXs.graphics, m_pCopyHelper, conf);

}

void SimpleRender::InitPresentation(VkSurfaceKHR &a_surface)
{
  m_surface = a_surface;

  m_presentationResources.queue = m_swapchain.CreateSwapChain(m_physicalDevice, m_device, m_surface,
                                                              m_width, m_height, m_framesInFlight, m_vsync);
  m_presentationResources.currentFrame = 0;

  VkSemaphoreCreateInfo semaphoreInfo = {};
  semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
  VK_CHECK_RESULT(vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_presentationResources.imageAvailable));
  VK_CHECK_RESULT(vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_presentationResources.renderingFinished));
  m_screenRenderPass = vk_utils::createDefaultRenderPass(m_device, m_swapchain.GetFormat());

  std::vector<VkFormat> depthFormats = {
      VK_FORMAT_D32_SFLOAT,
      VK_FORMAT_D32_SFLOAT_S8_UINT,
      VK_FORMAT_D24_UNORM_S8_UINT,
      VK_FORMAT_D16_UNORM_S8_UINT,
      VK_FORMAT_D16_UNORM
  };
  vk_utils::getSupportedDepthFormat(m_physicalDevice, depthFormats, &m_depthBuffer.format);
  m_depthBuffer  = vk_utils::createDepthTexture(m_device, m_physicalDevice, m_width, m_height, m_depthBuffer.format);
  std::array<VkImageLayout, 3> layouts = {
    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
  };
  for (uint32_t i = 0; i < framesSequence.size(); ++i)
  {
    framesSequence[i].format = m_swapchain.GetFormat();

    VkImageCreateInfo imgCreateInfo = {};
    imgCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imgCreateInfo.pNext = nullptr;
    imgCreateInfo.flags = 0;
    imgCreateInfo.imageType = VK_IMAGE_TYPE_2D;
    imgCreateInfo.format = m_swapchain.GetFormat();
    imgCreateInfo.extent = VkExtent3D{ m_width, m_height, 1u };
    imgCreateInfo.mipLevels = 1;
    imgCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imgCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imgCreateInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;
    imgCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imgCreateInfo.initialLayout = layouts[i];
    imgCreateInfo.arrayLayers = 1;

    VkImageViewCreateInfo imageViewInfo = {};
    imageViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    imageViewInfo.flags = 0;
    imageViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    imageViewInfo.format = m_swapchain.GetFormat();
    imageViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    imageViewInfo.subresourceRange.baseMipLevel = 0;
    imageViewInfo.subresourceRange.baseArrayLayer = 0;
    imageViewInfo.subresourceRange.layerCount = 1;
    imageViewInfo.subresourceRange.levelCount = 1;
    imageViewInfo.image = VK_NULL_HANDLE;

    vk_utils::createImgAllocAndBind(m_device, m_physicalDevice, m_width, m_height, m_swapchain.GetFormat(),
      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, &framesSequence[i], &imgCreateInfo, &imageViewInfo);
    std::vector<VkImageView> attachments = 
    {
      framesSequence[i].view,
      m_depthBuffer.view
    };

    VkFramebufferCreateInfo framebufferInfo = {};
    framebufferInfo.sType                   = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass              = m_screenRenderPass;
    framebufferInfo.attachmentCount         = (uint32_t)attachments.size();
    framebufferInfo.pAttachments            = attachments.data();
    framebufferInfo.width                   = m_width;
    framebufferInfo.height                  = m_height;
    framebufferInfo.layers                  = 1;

    vkCreateFramebuffer(m_device, &framebufferInfo, nullptr, &mainFramebuffers[i]);
    
  }
  defaultSampler = vk_utils::createSampler(m_device);
  m_frameBuffers = vk_utils::createFrameBuffers(m_device, m_swapchain, m_screenRenderPass, m_depthBuffer.view);

  m_pGUIRender = std::make_shared<ImGuiRender>(m_instance, m_device, m_physicalDevice, m_queueFamilyIDXs.graphics, m_graphicsQueue, m_swapchain);

  SetupQuadRenderer();
}

void SimpleRender::CreateInstance()
{
  VkApplicationInfo appInfo = {};
  appInfo.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  appInfo.pNext              = nullptr;
  appInfo.pApplicationName   = "VkRender";
  appInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
  appInfo.pEngineName        = "RayTracingSample";
  appInfo.engineVersion      = VK_MAKE_VERSION(0, 1, 0);
  appInfo.apiVersion         = VK_MAKE_VERSION(1, 1, 0);

  m_instance = vk_utils::createInstance(m_enableValidation, m_validationLayers, m_instanceExtensions, &appInfo);

  if (m_enableValidation)
    vk_utils::initDebugReportCallback(m_instance, &debugReportCallbackFn, &m_debugReportCallback);
}

void SimpleRender::CreateDevice(uint32_t a_deviceId)
{
  SetupDeviceExtensions();
  m_physicalDevice = vk_utils::findPhysicalDevice(m_instance, true, a_deviceId, m_deviceExtensions);

  SetupDeviceFeatures();
  m_device = vk_utils::createLogicalDevice(m_physicalDevice, m_validationLayers, m_deviceExtensions,
                                           m_enabledDeviceFeatures, m_queueFamilyIDXs,
                                           VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_TRANSFER_BIT, m_pDeviceFeatures);

  vkGetDeviceQueue(m_device, m_queueFamilyIDXs.graphics, 0, &m_graphicsQueue);
  vkGetDeviceQueue(m_device, m_queueFamilyIDXs.transfer, 0, &m_transferQueue);
}

void SimpleRender::SetupSimplePipeline()
{
  m_pBindings->BindBegin(VK_SHADER_STAGE_FRAGMENT_BIT|VK_SHADER_STAGE_VERTEX_BIT);
  m_pBindings->BindBuffer(0, m_ubo, VK_NULL_HANDLE, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
  m_pBindings->BindBuffer(1, appliedLightingBuffer, VK_NULL_HANDLE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
  m_pBindings->BindBuffer(2, indirectPointsBuffer, VK_NULL_HANDLE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
  m_pBindings->BindBuffer(3, m_pScnMgr->GetMaterialsBuffer(), VK_NULL_HANDLE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
  m_pBindings->BindBuffer(4, m_pScnMgr->GetMaterialPerVertexIDsBuffer(), VK_NULL_HANDLE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
  m_pBindings->BindAccelStruct(5, m_pScnMgr->GetTLAS(), VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR);
  m_pBindings->BindImageArray(6, m_pScnMgr->GetTextureViews(), m_pScnMgr->GetTextureSamplers());
  m_pBindings->BindBuffer(7, indirectPointsBuffer, VK_NULL_HANDLE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
  m_pBindings->BindEnd(&m_dSet, &m_dSetLayout);

  // if we are recreating pipeline (for example, to reload shaders)
  // we need to cleanup old pipeline
  if(m_basicForwardPipeline.layout != VK_NULL_HANDLE)
  {
    vkDestroyPipelineLayout(m_device, m_basicForwardPipeline.layout, nullptr);
    m_basicForwardPipeline.layout = VK_NULL_HANDLE;
  }
  if(m_basicForwardPipeline.pipeline != VK_NULL_HANDLE)
  {
    vkDestroyPipeline(m_device, m_basicForwardPipeline.pipeline, nullptr);
    m_basicForwardPipeline.pipeline = VK_NULL_HANDLE;
  }

  if(m_debugPointsPipeline.layout != VK_NULL_HANDLE)
  {
    vkDestroyPipelineLayout(m_device, m_debugPointsPipeline.layout, nullptr);
    m_debugPointsPipeline.layout = VK_NULL_HANDLE;
  }
  if(m_debugPointsPipeline.pipeline != VK_NULL_HANDLE)
  {
    vkDestroyPipeline(m_device, m_debugPointsPipeline.pipeline, nullptr);
    m_debugPointsPipeline.pipeline = VK_NULL_HANDLE;
  }

  if(m_debugLinesPipeline.layout != VK_NULL_HANDLE)
  {
    vkDestroyPipelineLayout(m_device, m_debugLinesPipeline.layout, nullptr);
    m_debugLinesPipeline.layout = VK_NULL_HANDLE;
  }
  if(m_debugLinesPipeline.pipeline != VK_NULL_HANDLE)
  {
    vkDestroyPipeline(m_device, m_debugLinesPipeline.pipeline, nullptr);
    m_debugLinesPipeline.pipeline = VK_NULL_HANDLE;
  }

  if(m_debugCubesPipeline.layout != VK_NULL_HANDLE)
  {
    vkDestroyPipelineLayout(m_device, m_debugCubesPipeline.layout, nullptr);
    m_debugCubesPipeline.layout = VK_NULL_HANDLE;
  }
  if(m_debugCubesPipeline.pipeline != VK_NULL_HANDLE)
  {
    vkDestroyPipeline(m_device, m_debugCubesPipeline.pipeline, nullptr);
    m_debugCubesPipeline.pipeline = VK_NULL_HANDLE;
  }

  vk_utils::GraphicsPipelineMaker maker;

  std::unordered_map<VkShaderStageFlagBits, std::string> shader_paths;
  shader_paths[VK_SHADER_STAGE_FRAGMENT_BIT] = FRAGMENT_SHADER_PATH + ".spv";
  shader_paths[VK_SHADER_STAGE_VERTEX_BIT]   = VERTEX_SHADER_PATH + ".spv";

  maker.LoadShaders(m_device, shader_paths);

  m_basicForwardPipeline.layout = maker.MakeLayout(m_device, {m_dSetLayout}, sizeof(pushConst2M));
  maker.SetDefaultState(m_width, m_height);

  m_basicForwardPipeline.pipeline = maker.MakePipeline(m_device, m_pScnMgr->GetPipelineVertexInputStateCreateInfo(),
                                                       m_screenRenderPass, {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR});

  {
    m_pBindings->BindBegin(VK_SHADER_STAGE_VERTEX_BIT);
    m_pBindings->BindBuffer(0, samplePointsBuffer, VK_NULL_HANDLE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    // m_pBindings->BindBuffer(0, voxelCenterBuffer, VK_NULL_HANDLE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    m_pBindings->BindEnd(&pointsdSet, &pointsdSetLayout);
    std::unordered_map<VkShaderStageFlagBits, std::string> shader_paths;
    shader_paths[VK_SHADER_STAGE_FRAGMENT_BIT] = "../../resources/shaders/debug_points.frag.spv";
    shader_paths[VK_SHADER_STAGE_VERTEX_BIT]   = "../../resources/shaders/debug_points.vert.spv";

    maker.LoadShaders(m_device, shader_paths);

    m_debugPointsPipeline.layout = maker.MakeLayout(m_device, {pointsdSetLayout}, sizeof(pushConst2M));
    maker.SetDefaultState(m_width, m_height);

    VkPipelineVertexInputStateCreateInfo vertInfo = {};
    vertInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo ia = {};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    m_debugPointsPipeline.pipeline = maker.MakePipeline(m_device, vertInfo,
                                                        m_screenRenderPass, {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR}, ia);
  }

  {
    m_pBindings->BindBegin(VK_SHADER_STAGE_VERTEX_BIT);
    m_pBindings->BindBuffer(0, indirectPointsBuffer, VK_NULL_HANDLE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    m_pBindings->BindBuffer(1, samplePointsBuffer, VK_NULL_HANDLE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    m_pBindings->BindEnd(&cubesdSet, &cubesdSetLayout);
    std::unordered_map<VkShaderStageFlagBits, std::string> shader_paths;
    shader_paths[VK_SHADER_STAGE_FRAGMENT_BIT] = "../../resources/shaders/debug_cubes.frag.spv";
    shader_paths[VK_SHADER_STAGE_VERTEX_BIT]   = "../../resources/shaders/debug_cubes.vert.spv";

    maker.LoadShaders(m_device, shader_paths);

    m_debugCubesPipeline.layout = maker.MakeLayout(m_device, {cubesdSetLayout}, sizeof(pushConst2M));
    maker.SetDefaultState(m_width, m_height);

    VkPipelineVertexInputStateCreateInfo vertInfo = {};
    vertInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    m_debugCubesPipeline.pipeline = maker.MakePipeline(m_device, vertInfo,
                                                        m_screenRenderPass, {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR});
  }

  {
    m_pBindings->BindBegin(VK_SHADER_STAGE_VERTEX_BIT);
    m_pBindings->BindBuffer(0, samplePointsBuffer, VK_NULL_HANDLE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    m_pBindings->BindBuffer(1, debugBuffer, VK_NULL_HANDLE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    m_pBindings->BindEnd(&pointsdSet, &pointsdSetLayout);
    std::unordered_map<VkShaderStageFlagBits, std::string> shader_paths;
    shader_paths[VK_SHADER_STAGE_FRAGMENT_BIT] = "../../resources/shaders/debug_lines.frag.spv";
    shader_paths[VK_SHADER_STAGE_VERTEX_BIT]   = "../../resources/shaders/debug_lines.vert.spv";

    maker.LoadShaders(m_device, shader_paths);

    m_debugLinesPipeline.layout = maker.MakeLayout(m_device, {pointsdSetLayout}, sizeof(pushConst2M));
    maker.SetDefaultState(m_width, m_height);

    VkPipelineVertexInputStateCreateInfo vertInfo = {};
    vertInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo ia = {};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    m_debugLinesPipeline.pipeline = maker.MakePipeline(m_device, vertInfo,
                                                        m_screenRenderPass, {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR}, ia);
  }

  {
    for (uint32_t i = 0; i < framesSequence.size(); ++i)
    {
      m_pBindings->BindBegin(VK_SHADER_STAGE_FRAGMENT_BIT);
      m_pBindings->BindImage(0, framesSequence[i].view, defaultSampler, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
      m_pBindings->BindImage(1, framesSequence[(i + framesSequence.size() - 1) % framesSequence.size()].view, defaultSampler, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
      m_pBindings->BindEnd(&temporalAccumdSet[i], &temporalAccumdSetLayout[i]);
    }
    std::unordered_map<VkShaderStageFlagBits, std::string> shader_paths;
    shader_paths[VK_SHADER_STAGE_FRAGMENT_BIT] = "../../resources/shaders/temporal_accum.frag.spv";
    shader_paths[VK_SHADER_STAGE_VERTEX_BIT]   = "../../resources/shaders/temporal_accum.vert.spv";

    maker.LoadShaders(m_device, shader_paths);

    m_temporalAccumPipeline.layout = maker.MakeLayout(m_device, {temporalAccumdSetLayout[0]}, sizeof(pushConst2M));
    maker.SetDefaultState(m_width, m_height);

    VkPipelineVertexInputStateCreateInfo vertInfo = {};
    vertInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo ia = {};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    m_temporalAccumPipeline.pipeline = maker.MakePipeline(m_device, vertInfo,
                                                        m_screenRenderPass, {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR}, ia);
  }

}

float radicalInverse_VdC(uint bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10; // / 0x100000000
}

vec2 hammersley2d(uint i, uint N) {
    return vec2(float(i)/float(N), radicalInverse_VdC(i));
}

void SimpleRender::setObjectName(VkBuffer buffer, const char *name)
{
  if (!vkDebugMarkerSetObjectNameEXT)
    return;
  VkDebugMarkerObjectNameInfoEXT nameInfo = {};
  nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_MARKER_OBJECT_NAME_INFO_EXT;
  nameInfo.objectType = VK_DEBUG_REPORT_OBJECT_TYPE_BUFFER_EXT;
  nameInfo.object = (uint64_t)buffer;
  nameInfo.pObjectName = name;
  vkDebugMarkerSetObjectNameEXT(m_device, &nameInfo);
}

void SimpleRender::CreateUniformBuffer()
{
  VkMemoryRequirements memReq;
  m_ubo = vk_utils::createBuffer(m_device, sizeof(UniformParams), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, &memReq);

  VkMemoryAllocateInfo allocateInfo = {};
  allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocateInfo.pNext = nullptr;
  allocateInfo.allocationSize = memReq.size;
  allocateInfo.memoryTypeIndex = vk_utils::findMemoryType(memReq.memoryTypeBits,
                                                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                                          m_physicalDevice);
  VK_CHECK_RESULT(vkAllocateMemory(m_device, &allocateInfo, nullptr, &m_uboAlloc));

  VK_CHECK_RESULT(vkBindBufferMemory(m_device, m_ubo, m_uboAlloc, 0));

  vkMapMemory(m_device, m_uboAlloc, 0, sizeof(m_uniforms), 0, &m_uboMappedMem);

  m_uniforms.lightPos  = LiteMath::float4(0.0f, 1.0f,  1.0f, 1.0f);
  // m_uniforms.lightPos  = LiteMath::float4(0.685000002f, 50.0000000f,  -39.3330002f, 1.0f);
  m_uniforms.lightPos  = LiteMath::float4(0.0f, 36.0000000f, 4.9f, 1.0f);
  m_uniforms.exposureValue = 1.f;

  UpdateUniformBuffer(0.0f);

  float3 gridF = to_float3((sceneBbox.boxMax - sceneBbox.boxMin) / VOXEL_SIZE);
  voxelsGrid = uint3(std::ceil(gridF.x), std::ceil(gridF.y), std::ceil(gridF.z));
  voxelsCount = voxelsGrid.x * voxelsGrid.y * voxelsGrid.z;
  maxPointsCount = voxelsCount * 6 * PER_SURFACE_POINTS;
  std::cout << "Voxels count " << voxelsCount << std::endl;
  visibleVoxelsApproxCount = voxelsCount * 0.275f;
  visibleVoxelsApproxCount = voxelsCount * 0.6f;
  std::cout << "Approximate visible voxels count " << visibleVoxelsApproxCount << std::endl;

  {
    VkMemoryRequirements memReq;
    pointsBuffer = vk_utils::createBuffer(m_device, sizeof(float4) * PER_SURFACE_POINTS, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT, &memReq);
    setObjectName(pointsBuffer, "random_points");

    VkMemoryAllocateInfo allocateInfo = {};
    allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.pNext = nullptr;
    allocateInfo.allocationSize = memReq.size;
    allocateInfo.memoryTypeIndex = vk_utils::findMemoryType(memReq.memoryTypeBits,
                                                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                                            m_physicalDevice);
    VK_CHECK_RESULT(vkAllocateMemory(m_device, &allocateInfo, nullptr, &pointsMem));

    VK_CHECK_RESULT(vkBindBufferMemory(m_device, pointsBuffer, pointsMem, 0));

    std::vector<float4> points;
    srand(0);
    for (uint32_t i = 0; i < PER_SURFACE_POINTS; ++i)
    {
      float2 p = hammersley2d(i, PER_SURFACE_POINTS) - 0.5;
      points.push_back(float4(p.x, p.y, 0, 0));
    }
    pointsToDraw = points.size();
    m_pCopyHelper->UpdateBuffer(pointsBuffer, 0, points.data(), points.size() * sizeof(float4));
  }
  {
    VkMemoryRequirements memReq;
    indirectPointsBuffer = vk_utils::createBuffer(m_device, sizeof(uint4) * voxelsCount, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT, &memReq);
    setObjectName(indirectPointsBuffer, "point_counters");

    VkMemoryAllocateInfo allocateInfo = {};
    allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.pNext = nullptr;
    allocateInfo.allocationSize = memReq.size;
    allocateInfo.memoryTypeIndex = vk_utils::findMemoryType(memReq.memoryTypeBits,
                                                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                                            m_physicalDevice);
    VK_CHECK_RESULT(vkAllocateMemory(m_device, &allocateInfo, nullptr, &indirectPointsMem));

    VK_CHECK_RESULT(vkBindBufferMemory(m_device, indirectPointsBuffer, indirectPointsMem, 0));
  }
  {
    VkMemoryRequirements memReq;
    samplePointsBuffer = vk_utils::createBuffer(m_device, sizeof(float4) * 3 * maxPointsCount, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &memReq);
    setObjectName(samplePointsBuffer, "samples");

    VkMemoryAllocateInfo allocateInfo = {};
    allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.pNext = nullptr;
    allocateInfo.allocationSize = memReq.size;
    allocateInfo.memoryTypeIndex = vk_utils::findMemoryType(memReq.memoryTypeBits,
                                                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                                            m_physicalDevice);
    VK_CHECK_RESULT(vkAllocateMemory(m_device, &allocateInfo, nullptr, &samplePointsMem));

    VK_CHECK_RESULT(vkBindBufferMemory(m_device, samplePointsBuffer, samplePointsMem, 0));
  }
  {
    trianglesCount = 0;
    for (uint32_t i = 0; i < m_pScnMgr->InstancesNum(); ++i)
    {
      trianglesCount += m_pScnMgr->GetMeshInfo(m_pScnMgr->GetInstanceInfo(i).mesh_id).m_indNum;
    }
    trianglesCount /= 3;
    VkMemoryRequirements memReq;
    primCounterBuffer = vk_utils::createBuffer(m_device, sizeof(uint32_t) * trianglesCount, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT, &memReq);
    setObjectName(primCounterBuffer, "samplesPerTriangles");

    VkMemoryAllocateInfo allocateInfo = {};
    allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.pNext = nullptr;
    allocateInfo.allocationSize = memReq.size;
    allocateInfo.memoryTypeIndex = vk_utils::findMemoryType(memReq.memoryTypeBits,
                                                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                                            m_physicalDevice);
    VK_CHECK_RESULT(vkAllocateMemory(m_device, &allocateInfo, nullptr, &primCounterMem));

    VK_CHECK_RESULT(vkBindBufferMemory(m_device, primCounterBuffer, primCounterMem, 0));
  }

  {
    clustersCount = visibleVoxelsApproxCount * PER_VOXEL_CLUSTERS;
    approxColumns = visibleVoxelsApproxCount * 0.03f;
    approxColumns = visibleVoxelsApproxCount * 0.27f;
    VkMemoryRequirements memReq;
    FFClusteredBuffer = vk_utils::createBuffer(m_device, 2 * sizeof(float) * approxColumns * clustersCount,
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT|VK_BUFFER_USAGE_TRANSFER_SRC_BIT, &memReq);
    setObjectName(FFClusteredBuffer, "FF");

    VkMemoryAllocateInfo allocateInfo = {};
    allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.pNext = nullptr;
    allocateInfo.allocationSize = memReq.size;
    allocateInfo.memoryTypeIndex = vk_utils::findMemoryType(memReq.memoryTypeBits,
                                                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                                            m_physicalDevice);
    VK_CHECK_RESULT(vkAllocateMemory(m_device, &allocateInfo, nullptr, &FFClusteredMem));

    VK_CHECK_RESULT(vkBindBufferMemory(m_device, FFClusteredBuffer, FFClusteredMem, 0));
  }

  {
    VkMemoryRequirements memReq;
    initLightingBuffer = vk_utils::createBuffer(m_device, sizeof(float4) * clustersCount, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &memReq);
    setObjectName(initLightingBuffer, "initial_lighting");

    VkMemoryAllocateInfo allocateInfo = {};
    allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.pNext = nullptr;
    allocateInfo.allocationSize = memReq.size;
    allocateInfo.memoryTypeIndex = vk_utils::findMemoryType(memReq.memoryTypeBits,
                                                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                                            m_physicalDevice);
    VK_CHECK_RESULT(vkAllocateMemory(m_device, &allocateInfo, nullptr, &initLightingMem));

    VK_CHECK_RESULT(vkBindBufferMemory(m_device, initLightingBuffer, initLightingMem, 0));
  }

  {
    VkMemoryRequirements memReq;
    reflLightingBuffer = vk_utils::createBuffer(m_device, sizeof(float4) * clustersCount, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &memReq);
    setObjectName(reflLightingBuffer, "reflected_lighting");

    VkMemoryAllocateInfo allocateInfo = {};
    allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.pNext = nullptr;
    allocateInfo.allocationSize = memReq.size;
    allocateInfo.memoryTypeIndex = vk_utils::findMemoryType(memReq.memoryTypeBits,
                                                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                                            m_physicalDevice);
    VK_CHECK_RESULT(vkAllocateMemory(m_device, &allocateInfo, nullptr, &reflLightingMem));

    VK_CHECK_RESULT(vkBindBufferMemory(m_device, reflLightingBuffer, reflLightingMem, 0));
  }

  {
    VkMemoryRequirements memReq;
    debugIndirBuffer = vk_utils::createBuffer(m_device, sizeof(uint) * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT, &memReq);
    setObjectName(debugIndirBuffer, "debug_lines_counter");

    VkMemoryAllocateInfo allocateInfo = {};
    allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.pNext = nullptr;
    allocateInfo.allocationSize = memReq.size;
    allocateInfo.memoryTypeIndex = vk_utils::findMemoryType(memReq.memoryTypeBits,
                                                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                                            m_physicalDevice);
    VK_CHECK_RESULT(vkAllocateMemory(m_device, &allocateInfo, nullptr, &debugIndirMem));

    VK_CHECK_RESULT(vkBindBufferMemory(m_device, debugIndirBuffer, debugIndirMem, 0));
  }

  {
    VkMemoryRequirements memReq;
    uint32_t debugLinesCnt = 1000;//maxPointsCount;
    debugBuffer = vk_utils::createBuffer(m_device, 2 * sizeof(uint) * debugLinesCnt, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &memReq);
    setObjectName(debugBuffer, "debug_lines");

    VkMemoryAllocateInfo allocateInfo = {};
    allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.pNext = nullptr;
    allocateInfo.allocationSize = memReq.size;
    allocateInfo.memoryTypeIndex = vk_utils::findMemoryType(memReq.memoryTypeBits,
                                                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                                            m_physicalDevice);
    VK_CHECK_RESULT(vkAllocateMemory(m_device, &allocateInfo, nullptr, &debugMem));

    VK_CHECK_RESULT(vkBindBufferMemory(m_device, debugBuffer, debugMem, 0));
  }

  {
    VkMemoryRequirements memReq;
    nonEmptyVoxelsBuffer = vk_utils::createBuffer(m_device, sizeof(uint) * voxelsCount, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &memReq);
    setObjectName(nonEmptyVoxelsBuffer, "visible_voxels");

    VkMemoryAllocateInfo allocateInfo = {};
    allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.pNext = nullptr;
    allocateInfo.allocationSize = memReq.size;
    allocateInfo.memoryTypeIndex = vk_utils::findMemoryType(memReq.memoryTypeBits,
                                                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                                            m_physicalDevice);
    VK_CHECK_RESULT(vkAllocateMemory(m_device, &allocateInfo, nullptr, &nonEmptyVoxelsMem));

    VK_CHECK_RESULT(vkBindBufferMemory(m_device, nonEmptyVoxelsBuffer, nonEmptyVoxelsMem, 0));
  }

  {
    VkMemoryRequirements memReq;
    indirVoxelsBuffer = vk_utils::createBuffer(m_device, sizeof(uint) * 4 * 2,
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT|VK_BUFFER_USAGE_TRANSFER_SRC_BIT, &memReq);
    setObjectName(indirVoxelsBuffer, "visible_voxels_counter");

    VkMemoryAllocateInfo allocateInfo = {};
    allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.pNext = nullptr;
    allocateInfo.allocationSize = memReq.size;
    allocateInfo.memoryTypeIndex = vk_utils::findMemoryType(memReq.memoryTypeBits,
                                                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                                            m_physicalDevice);
    VK_CHECK_RESULT(vkAllocateMemory(m_device, &allocateInfo, nullptr, &indirVoxelsMem));

    VK_CHECK_RESULT(vkBindBufferMemory(m_device, indirVoxelsBuffer, indirVoxelsMem, 0));
  }

  {
    VkMemoryRequirements memReq;
    appliedLightingBuffer = vk_utils::createBuffer(m_device, sizeof(float4) * voxelsCount * 6, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT, &memReq);
    setObjectName(appliedLightingBuffer, "final_lighting");

    VkMemoryAllocateInfo allocateInfo = {};
    allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.pNext = nullptr;
    allocateInfo.allocationSize = memReq.size;
    allocateInfo.memoryTypeIndex = vk_utils::findMemoryType(memReq.memoryTypeBits,
                                                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                                            m_physicalDevice);
    VK_CHECK_RESULT(vkAllocateMemory(m_device, &allocateInfo, nullptr, &appliedLightingMem));

    VK_CHECK_RESULT(vkBindBufferMemory(m_device, appliedLightingBuffer, appliedLightingMem, 0));
  }

  {
    VkMemoryRequirements memReq;
    ffRowLenBuffer = vk_utils::createBuffer(m_device, sizeof(uint32_t) * (clustersCount + 1),
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT|VK_BUFFER_USAGE_TRANSFER_SRC_BIT, &memReq);
    setObjectName(ffRowLenBuffer, "ff_row_lengths");

    VkMemoryAllocateInfo allocateInfo = {};
    allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.pNext = nullptr;
    allocateInfo.allocationSize = memReq.size;
    allocateInfo.memoryTypeIndex = vk_utils::findMemoryType(memReq.memoryTypeBits,
                                                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                                            m_physicalDevice);
    VK_CHECK_RESULT(vkAllocateMemory(m_device, &allocateInfo, nullptr, &ffRowLenMem));

    VK_CHECK_RESULT(vkBindBufferMemory(m_device, ffRowLenBuffer, ffRowLenMem, 0));
  }

  {
    VkMemoryRequirements memReq;
    ffTmpRowBuffer = vk_utils::createBuffer(m_device, sizeof(float) * clustersCount * 6, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &memReq);
    setObjectName(ffTmpRowBuffer, "ff_tmp_row");

    VkMemoryAllocateInfo allocateInfo = {};
    allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.pNext = nullptr;
    allocateInfo.allocationSize = memReq.size;
    allocateInfo.memoryTypeIndex = vk_utils::findMemoryType(memReq.memoryTypeBits,
                                                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                                            m_physicalDevice);
    VK_CHECK_RESULT(vkAllocateMemory(m_device, &allocateInfo, nullptr, &ffTmpRowMem));

    VK_CHECK_RESULT(vkBindBufferMemory(m_device, ffTmpRowBuffer, ffTmpRowMem, 0));
  }
}

float modify(float x)
{
  float q = std::abs(std::fmod(x, 2.0f) - 1.0f);
  return 6 * q * q * q * q * q - 15 * q * q * q * q + 10 * q * q * q;
}

void SimpleRender::UpdateUniformBuffer(float a_time)
{
  m_uniforms.lightPos.z = modify(a_time * lightSpeed) * (4.9f+ 6.8f) - 6.8f;
// most uniforms are updated in GUI -> SetupGUIElements()
  m_uniforms.time = a_time;
  m_uniforms.bmin = to_float3(sceneBbox.boxMin);
  m_uniforms.bmax = to_float3(sceneBbox.boxMax);
  m_uniforms.voxelSize = VOXEL_SIZE;
  m_uniforms.interpolation = (interpolation ? 1 : 0) | (directLight ? 2 : 0) | (indirectLight ? 4 : 0)
    | (tonemapping ? 8 : 0);
  memcpy(m_uboMappedMem, &m_uniforms, sizeof(m_uniforms));
}

void SimpleRender::BuildCommandBufferSimple(VkCommandBuffer a_cmdBuff, VkFramebuffer a_frameBuff,
                                            SwapchainAttachment swapchainData, VkPipeline a_pipeline)
{
  vkResetCommandBuffer(a_cmdBuff, 0);

  VkCommandBufferBeginInfo beginInfo = {};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;

  VK_CHECK_RESULT(vkBeginCommandBuffer(a_cmdBuff, &beginInfo));

  vk_utils::setDefaultViewport(a_cmdBuff, static_cast<float>(m_width), static_cast<float>(m_height));
  vk_utils::setDefaultScissor(a_cmdBuff, m_width, m_height);

  ///// draw final scene to screen
  {
    VkRenderPassBeginInfo renderPassInfo = {};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = m_screenRenderPass;
    renderPassInfo.framebuffer = mainFramebuffers[0];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = m_swapchain.GetExtent();

    VkClearValue clearValues[2] = {};
    clearValues[0].color = {0.0f, 0.0f, 0.0f, 1.0f};
    clearValues[1].depthStencil = {1.0f, 0};
    renderPassInfo.clearValueCount = 2;
    renderPassInfo.pClearValues = &clearValues[0];

    vkCmdBeginRenderPass(a_cmdBuff, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(a_cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, a_pipeline);

    vkCmdBindDescriptorSets(a_cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, m_basicForwardPipeline.layout, 0, 1,
                            &m_dSet, 0, VK_NULL_HANDLE);

    VkShaderStageFlags stageFlags = (VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);

    VkDeviceSize zero_offset = 0u;
    VkBuffer vertexBuf = m_pScnMgr->GetVertexBuffer();
    VkBuffer indexBuf = m_pScnMgr->GetIndexBuffer();

    vkCmdBindVertexBuffers(a_cmdBuff, 0, 1, &vertexBuf, &zero_offset);
    vkCmdBindIndexBuffer(a_cmdBuff, indexBuf, 0, VK_INDEX_TYPE_UINT32);

    for (uint32_t i = 0; i < m_pScnMgr->InstancesNum(); ++i)
    {
      auto inst = m_pScnMgr->GetInstanceInfo(i);

      pushConst2M.model = m_pScnMgr->GetInstanceMatrix(i);
      auto mesh_info = m_pScnMgr->GetMeshInfo(inst.mesh_id);
      vkCmdPushConstants(a_cmdBuff, m_basicForwardPipeline.layout, stageFlags, 0,
                         sizeof(pushConst2M), &pushConst2M);

      vkCmdDrawIndexed(a_cmdBuff, mesh_info.m_indNum, 1, mesh_info.m_indexOffset, mesh_info.m_vertexOffset, 0);
    }

    if (debugPoints)
    {
      vkCmdBindPipeline(a_cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, m_debugPointsPipeline.pipeline);

      vkCmdBindDescriptorSets(a_cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, m_debugPointsPipeline.layout, 0, 1,
                              &pointsdSet, 0, VK_NULL_HANDLE);

      struct KernelArgsPC
      {
        LiteMath::float4x4 projView;
        uint32_t perFacePointsCount;
      } pcData;
      pcData.projView = pushConst2M.projView;
      pcData.perFacePointsCount = PER_SURFACE_POINTS;
      vkCmdPushConstants(a_cmdBuff, m_debugPointsPipeline.layout, stageFlags, 0,
                          sizeof(pcData), &pcData);
      
      // vkCmdDraw(a_cmdBuff, voxelsCount, 1, 0, 0);
      vkCmdDrawIndirect(a_cmdBuff, indirectPointsBuffer, 0, voxelsCount, sizeof(uint32_t) * 4);
    }

    if (debugCubes)
    {
      vkCmdBindPipeline(a_cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, m_debugCubesPipeline.pipeline);

      vkCmdBindDescriptorSets(a_cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, m_debugCubesPipeline.layout, 0, 1,
                              &cubesdSet, 0, VK_NULL_HANDLE);

      struct KernelArgsPC
      {
        LiteMath::float4x4 projView;
        LiteMath::float3 bmin;
        uint32_t voxelsCount;
        LiteMath::float3 bmax;
        float voxelSize;
        uint32_t maxPointsPerVoxelCount;
        float debugCubesScale;
      } pcData;
      pcData.projView = pushConst2M.projView;
      pcData.bmin = to_float3(sceneBbox.boxMin);
      pcData.voxelsCount = voxelsCount;
      pcData.bmax = to_float3(sceneBbox.boxMax);
      pcData.voxelSize = VOXEL_SIZE;
      pcData.maxPointsPerVoxelCount = 6 * PER_SURFACE_POINTS;
      pcData.debugCubesScale = debugCubesScale;
      vkCmdPushConstants(a_cmdBuff, m_debugCubesPipeline.layout, stageFlags, 0,
                          sizeof(pcData), &pcData);
      
      vkCmdDraw(a_cmdBuff, 36, voxelsCount, 0, 0);
    }

    {
      vkCmdBindPipeline(a_cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, m_debugLinesPipeline.pipeline);

      vkCmdBindDescriptorSets(a_cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, m_debugLinesPipeline.layout, 0, 1,
                              &pointsdSet, 0, VK_NULL_HANDLE);

      struct KernelArgsPC
      {
        LiteMath::float4x4 projView;
        uint32_t perFacePointsCount;
      } pcData;
      pcData.projView = pushConst2M.projView;
      pcData.perFacePointsCount = PER_SURFACE_POINTS;
      vkCmdPushConstants(a_cmdBuff, m_debugLinesPipeline.layout, stageFlags, 0,
                          sizeof(pcData), &pcData);
      
      vkCmdDrawIndirect(a_cmdBuff, debugIndirBuffer, 0, 1, sizeof(uint32_t) * 4);
    }

    vkCmdEndRenderPass(a_cmdBuff);

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = framesSequence[0].image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    barrier.srcAccessMask = VK_IMAGE_ASPECT_COLOR_BIT; // TODO
    barrier.dstAccessMask = VK_IMAGE_ASPECT_COLOR_BIT; // TODO

    vkCmdPipelineBarrier(
        a_cmdBuff,
        VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT , VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT ,
        VK_DEPENDENCY_BY_REGION_BIT,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );

    renderPassInfo.framebuffer = mainFramebuffers[1];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = m_swapchain.GetExtent();

    vkCmdBeginRenderPass(a_cmdBuff, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    {
      vkCmdBindPipeline(a_cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, m_temporalAccumPipeline.pipeline);

      vkCmdBindDescriptorSets(a_cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, m_temporalAccumPipeline.layout, 0, 1,
                              &temporalAccumdSet[0], 0, VK_NULL_HANDLE);

      struct KernelArgsPC
      {
        float blendFactor;
      } pcData;
      pcData.blendFactor = blendFactor;
      vkCmdPushConstants(a_cmdBuff, m_temporalAccumPipeline.layout, stageFlags, 0,
                          sizeof(pcData), &pcData);
      
      vkCmdDraw(a_cmdBuff, 3, 1, 0, 0);
    }
    vkCmdEndRenderPass(a_cmdBuff);

    VkImageCopy cp{};
    cp.srcOffset = VkOffset3D{0, 0, 0};
    cp.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    cp.srcSubresource.layerCount = 1;
    cp.dstOffset = VkOffset3D{0, 0, 0};
    cp.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    cp.dstSubresource.layerCount = 1;
    cp.extent = VkExtent3D{m_swapchain.GetExtent().width, m_swapchain.GetExtent().height, 1};


    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.image = swapchainData.image;
    vkCmdPipelineBarrier(
        a_cmdBuff,
        VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT , VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT ,
        VK_DEPENDENCY_BY_REGION_BIT,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );

    barrier.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.image = framesSequence[1].image;
    vkCmdPipelineBarrier(
        a_cmdBuff,
        VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT , VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT ,
        VK_DEPENDENCY_BY_REGION_BIT,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );

    vkCmdCopyImage(a_cmdBuff, framesSequence[1].image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 
      swapchainData.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &cp);

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.image = framesSequence[1].image;
    vkCmdPipelineBarrier(
        a_cmdBuff,
        VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT , VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT ,
        VK_DEPENDENCY_BY_REGION_BIT,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrier.image = swapchainData.image;
    vkCmdPipelineBarrier(
        a_cmdBuff,
        VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT , VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT ,
        VK_DEPENDENCY_BY_REGION_BIT,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );
  }

  VK_CHECK_RESULT(vkEndCommandBuffer(a_cmdBuff));

  if (screenshotRequested)
  {
    std::vector<uint32_t> imageData(m_width * m_height);
    m_pCopyHelper->ReadImage(framesSequence[0].image, imageData.data(), m_width, m_height, 4, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    for (uint32_t &color : imageData)
    {
      color = 0xFF000000
        | (((color >> 16) & 0xFF) << 0)
        | (((color >> 8) & 0xFF) << 8)
        | (((color >> 0) & 0xFF) << 16);
    }
    std::time_t end_time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::string dateStr = std::ctime(&end_time);
    dateStr.resize(dateStr.size() - 1);
    std::replace(dateStr.begin(), dateStr.end(), ':', '_');
    std::replace(dateStr.begin(), dateStr.end(), ' ', '_');

    stbi_write_png((std::string("Screenshots/") + dateStr + ".png").c_str(), m_width, m_height, 4, imageData.data(), 4 * m_width);
  }

  std::swap(temporalAccumdSet[2], temporalAccumdSet[1]);
  std::swap(temporalAccumdSetLayout[2], temporalAccumdSetLayout[1]);
  std::swap(framesSequence[2], framesSequence[1]);
  std::swap(mainFramebuffers[2], mainFramebuffers[1]);
  std::swap(temporalAccumdSet[0], temporalAccumdSet[1]);
  std::swap(temporalAccumdSetLayout[0], temporalAccumdSetLayout[1]);
  std::swap(framesSequence[0], framesSequence[1]);
  std::swap(mainFramebuffers[0], mainFramebuffers[1]);
}

void SimpleRender::BuildCommandBufferQuad(VkCommandBuffer a_cmdBuff, VkImageView a_targetImageView)
{
  vkResetCommandBuffer(a_cmdBuff, 0);

  VkCommandBufferBeginInfo beginInfo = {};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;

  VK_CHECK_RESULT(vkBeginCommandBuffer(a_cmdBuff, &beginInfo));
  {
    float scaleAndOffset[4] = { 0.5f, 0.5f, -0.5f, +0.5f };
    m_pFSQuad->SetRenderTarget(a_targetImageView);
    m_pFSQuad->DrawCmd(a_cmdBuff, m_quadDS, scaleAndOffset);
  }

  VK_CHECK_RESULT(vkEndCommandBuffer(a_cmdBuff));
}

void SimpleRender::CleanupPipelineAndSwapchain()
{
  if (!m_cmdBuffersDrawMain.empty())
  {
    vkFreeCommandBuffers(m_device, m_commandPool, static_cast<uint32_t>(m_cmdBuffersDrawMain.size()),
                         m_cmdBuffersDrawMain.data());
    m_cmdBuffersDrawMain.clear();
  }

  for (size_t i = 0; i < m_frameFences.size(); i++)
  {
    vkDestroyFence(m_device, m_frameFences[i], nullptr);
  }
  m_frameFences.clear();

  vk_utils::deleteImg(m_device, &m_depthBuffer);

  for (size_t i = 0; i < m_frameBuffers.size(); i++)
  {
    vkDestroyFramebuffer(m_device, m_frameBuffers[i], nullptr);
  }
  m_frameBuffers.clear();

  if(m_screenRenderPass != VK_NULL_HANDLE)
  {
    vkDestroyRenderPass(m_device, m_screenRenderPass, nullptr);
    m_screenRenderPass = VK_NULL_HANDLE;
  }

  m_swapchain.Cleanup();
}

void SimpleRender::RecreateSwapChain()
{
  vkDeviceWaitIdle(m_device);

  CleanupPipelineAndSwapchain();
  auto oldImagesNum = m_swapchain.GetImageCount();
  m_presentationResources.queue = m_swapchain.CreateSwapChain(m_physicalDevice, m_device, m_surface, m_width, m_height,
    oldImagesNum, m_vsync);

  std::vector<VkFormat> depthFormats = {
      VK_FORMAT_D32_SFLOAT,
      VK_FORMAT_D32_SFLOAT_S8_UINT,
      VK_FORMAT_D24_UNORM_S8_UINT,
      VK_FORMAT_D16_UNORM_S8_UINT,
      VK_FORMAT_D16_UNORM
  };                                                            
  vk_utils::getSupportedDepthFormat(m_physicalDevice, depthFormats, &m_depthBuffer.format);
  
  m_screenRenderPass = vk_utils::createDefaultRenderPass(m_device, m_swapchain.GetFormat());
  m_depthBuffer      = vk_utils::createDepthTexture(m_device, m_physicalDevice, m_width, m_height, m_depthBuffer.format);
  m_frameBuffers     = vk_utils::createFrameBuffers(m_device, m_swapchain, m_screenRenderPass, m_depthBuffer.view);

  m_frameFences.resize(m_framesInFlight);
  VkFenceCreateInfo fenceInfo = {};
  fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
  for (size_t i = 0; i < m_framesInFlight; i++)
  {
    VK_CHECK_RESULT(vkCreateFence(m_device, &fenceInfo, nullptr, &m_frameFences[i]));
  }

  m_cmdBuffersDrawMain = vk_utils::createCommandBuffers(m_device, m_commandPool, m_framesInFlight);

  // *** ray tracing resources
  m_raytracedImageData.resize(m_width * m_height);
  m_pRayTracerGPU = nullptr;
  SetupRTImage();
  SetupQuadRenderer();
  SetupQuadDescriptors();
  //

  m_pGUIRender->OnSwapchainChanged(m_swapchain);
}

extern bool enableImgui;

void SimpleRender::ProcessInput(const AppInput &input)
{
  // add keyboard controls here
  // camera movement is processed separately

  // recreate pipeline to reload shaders
  if(input.keyPressed[GLFW_KEY_B])
  {
#ifdef WIN32
    std::system("cd ../../resources/shaders && python compile_simple_render_shaders.py");
#else
    std::system("cd ../../resources/shaders && python3 compile_simple_render_shaders.py");
#endif

    m_pRayTracerGPU.reset();
    SetupSimplePipeline();

    for (uint32_t i = 0; i < m_framesInFlight; ++i)
    {
      BuildCommandBufferSimple(m_cmdBuffersDrawMain[i], m_frameBuffers[i],
                               m_swapchain.GetAttachment(i), m_basicForwardPipeline.pipeline);
    }
  }

  if(input.keyPressed[GLFW_KEY_1])
  {
    m_currentRenderMode = RenderMode::RASTERIZATION;
  }
  else if(input.keyPressed[GLFW_KEY_2])
  {
    m_currentRenderMode = RenderMode::RAYTRACING;
  }

  if (input.keyPressed[GLFW_KEY_Z])
    enableImgui = !enableImgui;

}

void SimpleRender::UpdateCamera(const Camera* cams, uint32_t a_camsCount)
{
  assert(a_camsCount > 0);
  m_cam = cams[0];
  UpdateView();
}

void SimpleRender::UpdateView()
{
  const float aspect   = float(m_width) / float(m_height);
  auto mProjFix        = OpenglToVulkanProjectionMatrixFix();
  m_projectionMatrix   = projectionMatrix(m_cam.fov, aspect, 0.1f, 1000.0f);
  auto mLookAt         = LiteMath::lookAt(m_cam.pos, m_cam.lookAt, m_cam.up);
  auto mWorldViewProj = LiteMath::float4x4();
  if (temporalAccumulation)
  { 
    const int HALTON_COUNT = 8;
    const vec2 HALTON_SEQUENCE[8] = {
      vec2(1.0 / 2.0, 1.0 / 3.0),
      vec2(1.0 / 4.0, 2.0 / 3.0),
      vec2(3.0 / 4.0, 1.0 / 9.0),
      vec2(1.0 / 8.0, 4.0 / 9.0),
      vec2(5.0 / 8.0, 7.0 / 9.0),
      vec2(3.0 / 8.0, 2.0 / 9.0),
      vec2(7.0 / 8.0, 5.0 / 9.0),
      vec2(1.0 / 16.0, 8.0 / 9.0),
    };
    std::random_device dev;
    std::mt19937 rng(dev());
    std::uniform_int_distribution<std::mt19937::result_type> dist6(0,7);
    const float JITTER_SCALE = 2.0f;
    vec2 jitter = ((HALTON_SEQUENCE[dist6(rng) % HALTON_COUNT]) - 0.5f) * JITTER_SCALE / vec2(m_width, m_height);
    float4x4 JitterMat = LiteMath::float4x4();
    JitterMat(0,3) = jitter.x;
    JitterMat(1,3) = jitter.y;
    mWorldViewProj = mProjFix * JitterMat * m_projectionMatrix * mLookAt;
  }
  else
  {
    mWorldViewProj = mProjFix * m_projectionMatrix * mLookAt;
  }
  pushConst2M.projView = mWorldViewProj;
  // std::cout << "Yaw " << atan2(mLookAt[1][0], mLookAt[0][0])  << std::endl;
  // std::cout << "Pitch " << atan2(-mLookAt[2][0], sqrt(mLookAt[2][1] * mLookAt[2][1] + mLookAt[2][2] * mLookAt[2][2]))  << std::endl;

  m_inverseProjViewMatrix = LiteMath::inverse4x4(m_projectionMatrix * transpose(inverse4x4(mLookAt)));
}

void SimpleRender::LoadScene(const char* path)
{
  m_pScnMgr->LoadScene(path);
  m_pScnMgr->BuildAllBLAS();
  m_pScnMgr->BuildTLAS();
  GetBbox();

  std::vector<std::pair<VkDescriptorType, uint32_t> > dtypes = {
    {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,             1},
    {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,     1}
  };

  // set large a_maxSets, because every window resize will cause the descriptor set for quad being to be recreated
  m_pBindings = std::make_shared<vk_utils::DescriptorMaker>(m_device, dtypes, 1000);

  SetupRTImage();
  CreateUniformBuffer();

  SetupSimplePipeline();
  SetupQuadDescriptors();

  auto loadedCam = m_pScnMgr->GetCamera(0);
  m_cam.fov = loadedCam.fov;
  m_cam.pos = float3(loadedCam.pos);
  m_cam.up  = float3(loadedCam.up);
  m_cam.lookAt = float3(loadedCam.lookAt);
  m_cam.tdist  = loadedCam.farPlane;

  UpdateView();
}

void SimpleRender::DrawFrameSimple()
{
  vkWaitForFences(m_device, 1, &m_frameFences[m_presentationResources.currentFrame], VK_TRUE, UINT64_MAX);
  vkResetFences(m_device, 1, &m_frameFences[m_presentationResources.currentFrame]);

  uint32_t imageIdx;
  m_swapchain.AcquireNextImage(m_presentationResources.imageAvailable, &imageIdx);

  auto currentCmdBuf = m_cmdBuffersDrawMain[m_presentationResources.currentFrame];

  VkSemaphore waitSemaphores[] = {m_presentationResources.imageAvailable};
  VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};

  if(m_currentRenderMode == RenderMode::RASTERIZATION)
  {
    TraceGenSamples();
    BuildCommandBufferSimple(currentCmdBuf, m_frameBuffers[imageIdx], m_swapchain.GetAttachment(imageIdx), m_basicForwardPipeline.pipeline);
  }
  else if(m_currentRenderMode == RenderMode::RAYTRACING)
  {
    RayTraceGPU();

    BuildCommandBufferQuad(currentCmdBuf, m_swapchain.GetAttachment(imageIdx).view);
  }

  VkSubmitInfo submitInfo = {};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.waitSemaphoreCount = 1;
  submitInfo.pWaitSemaphores = waitSemaphores;
  submitInfo.pWaitDstStageMask = waitStages;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &currentCmdBuf;

  VkSemaphore signalSemaphores[] = {m_presentationResources.renderingFinished};
  submitInfo.signalSemaphoreCount = 1;
  submitInfo.pSignalSemaphores = signalSemaphores;

  VK_CHECK_RESULT(vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, m_frameFences[m_presentationResources.currentFrame]));

  VkResult presentRes = m_swapchain.QueuePresent(m_presentationResources.queue, imageIdx,
                                                 m_presentationResources.renderingFinished);

  if (presentRes == VK_ERROR_OUT_OF_DATE_KHR || presentRes == VK_SUBOPTIMAL_KHR)
  {
    RecreateSwapChain();
  }
  else if (presentRes != VK_SUCCESS)
  {
    RUN_TIME_ERROR("Failed to present swapchain image");
  }

  m_presentationResources.currentFrame = (m_presentationResources.currentFrame + 1) % m_framesInFlight;

  vkQueueWaitIdle(m_presentationResources.queue);
}

void SimpleRender::DrawFrame(float a_time, DrawMode a_mode)
{
  UpdateUniformBuffer(a_time);

  switch (a_mode)
  {
  case DrawMode::WITH_GUI:
    SetupGUIElements();
    DrawFrameWithGUI();
    break;
  case DrawMode::NO_GUI:
    DrawFrameSimple();
    break;
  default:
    DrawFrameSimple();
  }
}

void SimpleRender::Cleanup()
{
  m_pGUIRender = nullptr;
  ImGui::DestroyContext();
  CleanupPipelineAndSwapchain();
  if(m_surface != VK_NULL_HANDLE)
  {
    vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
    m_surface = VK_NULL_HANDLE;
  }

  if(m_rtImageSampler != VK_NULL_HANDLE)
  {
    vkDestroySampler(m_device, m_rtImageSampler, nullptr);
    m_rtImageSampler = VK_NULL_HANDLE;
  }
  vk_utils::deleteImg(m_device, &m_rtImage);

  m_pFSQuad = nullptr;

  if (m_basicForwardPipeline.pipeline != VK_NULL_HANDLE)
  {
    vkDestroyPipeline(m_device, m_basicForwardPipeline.pipeline, nullptr);
    m_basicForwardPipeline.pipeline = VK_NULL_HANDLE;
  }
  if (m_basicForwardPipeline.layout != VK_NULL_HANDLE)
  {
    vkDestroyPipelineLayout(m_device, m_basicForwardPipeline.layout, nullptr);
    m_basicForwardPipeline.layout = VK_NULL_HANDLE;
  }

  if (m_presentationResources.imageAvailable != VK_NULL_HANDLE)
  {
    vkDestroySemaphore(m_device, m_presentationResources.imageAvailable, nullptr);
    m_presentationResources.imageAvailable = VK_NULL_HANDLE;
  }
  if (m_presentationResources.renderingFinished != VK_NULL_HANDLE)
  {
    vkDestroySemaphore(m_device, m_presentationResources.renderingFinished, nullptr);
    m_presentationResources.renderingFinished = VK_NULL_HANDLE;
  }

  if (m_commandPool != VK_NULL_HANDLE)
  {
    vkDestroyCommandPool(m_device, m_commandPool, nullptr);
    m_commandPool = VK_NULL_HANDLE;
  }

  if(m_ubo != VK_NULL_HANDLE)
  {
    vkDestroyBuffer(m_device, m_ubo, nullptr);
    m_ubo = VK_NULL_HANDLE;
  }

  if(m_uboAlloc != VK_NULL_HANDLE)
  {
    vkFreeMemory(m_device, m_uboAlloc, nullptr);
    m_uboAlloc = VK_NULL_HANDLE;
  }

  if(pointsBuffer != VK_NULL_HANDLE)
  {
    vkDestroyBuffer(m_device, pointsBuffer, nullptr);
    pointsBuffer = VK_NULL_HANDLE;
  }

  if(pointsMem != VK_NULL_HANDLE)
  {
    vkFreeMemory(m_device, pointsMem, nullptr);
    pointsMem = VK_NULL_HANDLE;
  }

  if(m_genColorBuffer != VK_NULL_HANDLE)
  {
    vkDestroyBuffer(m_device, m_genColorBuffer, nullptr);
    m_genColorBuffer = VK_NULL_HANDLE;
  }

  if(m_colorMem != VK_NULL_HANDLE)
  {
    vkFreeMemory(m_device, m_colorMem, nullptr);
    m_colorMem = VK_NULL_HANDLE;
  }

  m_pRayTracerGPU = nullptr;

  m_pBindings = nullptr;
  m_pScnMgr   = nullptr;
  m_pCopyHelper = nullptr;

  if(m_device != VK_NULL_HANDLE)
  {
    vkDestroyDevice(m_device, nullptr);
    m_device = VK_NULL_HANDLE;
  }

  if(m_debugReportCallback != VK_NULL_HANDLE)
  {
    vkDestroyDebugReportCallbackEXT(m_instance, m_debugReportCallback, nullptr);
    m_debugReportCallback = VK_NULL_HANDLE;
  }

  if(m_instance != VK_NULL_HANDLE)
  {
    vkDestroyInstance(m_instance, nullptr);
    m_instance = VK_NULL_HANDLE;
  }
}

/////////////////////////////////

void SimpleRender::SetupGUIElements()
{
  ImGui_ImplVulkan_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();
  {
//    ImGui::ShowDemoWindow();
    ImGui::Begin("Your render settings here");
    ImGui::Text("Form-factors computation progress: %.2f%%", FFComputeProgress * 100.f);
    ImGui::NewLine();

    ImGui::SliderFloat3("Light source position", m_uniforms.lightPos.M, -50.f, 50.f);

    ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);

    ImGui::NewLine();
    ImGui::Checkbox("Interpolation: ", &interpolation);
    ImGui::Checkbox("Direct lighting: ", &directLight);
    ImGui::Checkbox("Indirect lighting: ", &indirectLight);
    ImGui::Checkbox("Debug points: ", &debugPoints);
    ImGui::Checkbox("Debug cubes: ", &debugCubes);
    ImGui::Checkbox("Update lighting: ", &updateLight);
    ImGui::Checkbox("Multiple bounce: ", &multibounce);
    ImGui::Checkbox("Tonemapping: ", &tonemapping);
    ImGui::Checkbox("Temporal accumulation: ", &temporalAccumulation);
    ImGui::SliderFloat("Exposure: ", &(m_uniforms.exposureValue), 0.1, 10.f);
    ImGui::SliderFloat("Blend factor: ", &(blendFactor), 0, 0.97f);
    ImGui::SliderFloat("Light speed: ", &(lightSpeed), 0.01, 0.5);
    
    screenshotRequested = ImGui::Button("Make screenshot");
    if (useAlias && !switchAlias)
      switchAlias = ImGui::Button("Use alias tables");
    
    
    
    ImGui::SliderFloat("Debug cubes scale:", &debugCubesScale, 0, 1);


    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f),"Press 'B' to recompile and reload shaders");
    ImGui::Text("Changing bindings is not supported.");
    ImGui::Text("Vertex shader path: %s", VERTEX_SHADER_PATH.c_str());
    ImGui::Text("Fragment shader path: %s", FRAGMENT_SHADER_PATH.c_str());
    ImGui::End();
  }

  // Rendering
  ImGui::Render();
}

void SimpleRender::DrawFrameWithGUI()
{
  vkWaitForFences(m_device, 1, &m_frameFences[m_presentationResources.currentFrame], VK_TRUE, UINT64_MAX);
  vkResetFences(m_device, 1, &m_frameFences[m_presentationResources.currentFrame]);

  uint32_t imageIdx;
  auto result = m_swapchain.AcquireNextImage(m_presentationResources.imageAvailable, &imageIdx);
  if (result == VK_ERROR_OUT_OF_DATE_KHR)
  {
    RecreateSwapChain();
    return;
  }
  else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
  {
    RUN_TIME_ERROR("Failed to acquire the next swapchain image!");
  }

  auto currentCmdBuf = m_cmdBuffersDrawMain[m_presentationResources.currentFrame];

  VkSemaphore waitSemaphores[] = {m_presentationResources.imageAvailable};
  VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};

  if(m_currentRenderMode == RenderMode::RASTERIZATION)
  {
    TraceGenSamples();
    BuildCommandBufferSimple(currentCmdBuf, m_frameBuffers[imageIdx], m_swapchain.GetAttachment(imageIdx), m_basicForwardPipeline.pipeline);
  }
  else if(m_currentRenderMode == RenderMode::RAYTRACING)
  {
      RayTraceGPU();

    BuildCommandBufferQuad(currentCmdBuf, m_swapchain.GetAttachment(imageIdx).view);
  }

  ImDrawData* pDrawData = ImGui::GetDrawData();
  auto currentGUICmdBuf = m_pGUIRender->BuildGUIRenderCommand(imageIdx, pDrawData);

  std::vector<VkCommandBuffer> submitCmdBufs = { currentCmdBuf, currentGUICmdBuf};

  VkSubmitInfo submitInfo = {};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.waitSemaphoreCount = 1;
  submitInfo.pWaitSemaphores = waitSemaphores;
  submitInfo.pWaitDstStageMask = waitStages;
  submitInfo.commandBufferCount = (uint32_t)submitCmdBufs.size();
  submitInfo.pCommandBuffers = submitCmdBufs.data();

  VkSemaphore signalSemaphores[] = {m_presentationResources.renderingFinished};
  submitInfo.signalSemaphoreCount = 1;
  submitInfo.pSignalSemaphores = signalSemaphores;

  VK_CHECK_RESULT(vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, m_frameFences[m_presentationResources.currentFrame]));

  VkResult presentRes = m_swapchain.QueuePresent(m_presentationResources.queue, imageIdx,
    m_presentationResources.renderingFinished);

  if (presentRes == VK_ERROR_OUT_OF_DATE_KHR || presentRes == VK_SUBOPTIMAL_KHR)
  {
    RecreateSwapChain();
  }
  else if (presentRes != VK_SUCCESS)
  {
    RUN_TIME_ERROR("Failed to present swapchain image");
  }

  m_presentationResources.currentFrame = (m_presentationResources.currentFrame + 1) % m_framesInFlight;

  vkQueueWaitIdle(m_presentationResources.queue);
}
