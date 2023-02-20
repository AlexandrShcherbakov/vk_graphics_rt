#include <render/VulkanRTX.h>
#include "simple_render.h"
#include "raytracing_generated.h"

// ***************************************************************************************************************************
// setup full screen quad to display ray traced image
void SimpleRender::SetupQuadRenderer()
{
  vk_utils::RenderTargetInfo2D rtargetInfo = {};
  rtargetInfo.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  rtargetInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  rtargetInfo.format = m_swapchain.GetFormat();
  rtargetInfo.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  rtargetInfo.size   = m_swapchain.GetExtent();
  m_pFSQuad.reset();
  m_pFSQuad = std::make_shared<vk_utils::QuadRenderer>(0,0, m_width, m_height);
  m_pFSQuad->Create(m_device, "../../resources/shaders/quad3_vert.vert.spv", "../../resources/shaders/my_quad.frag.spv", rtargetInfo);
}

void SimpleRender::SetupQuadDescriptors()
{
  m_pBindings->BindBegin(VK_SHADER_STAGE_FRAGMENT_BIT);
  m_pBindings->BindImage(0, m_rtImage.view, m_rtImageSampler, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
  m_pBindings->BindEnd(&m_quadDS, &m_quadDSLayout);
}

void SimpleRender::SetupRTImage()
{
  vk_utils::deleteImg(m_device, &m_rtImage);

  // change format and usage according to your implementation of RT
  m_rtImage.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  createImgAllocAndBind(m_device, m_physicalDevice, m_width, m_height, VK_FORMAT_R8G8B8A8_UNORM,
    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, &m_rtImage);

  if(m_rtImageSampler == VK_NULL_HANDLE)
  {
    m_rtImageSampler = vk_utils::createSampler(m_device, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK);
  }
}
// ***************************************************************************************************************************

// convert geometry data and pass it to acceleration structure builder
void SimpleRender::GetBbox()
{
  auto meshesData = m_pScnMgr->GetMeshData();
  for (size_t i = 0; i < m_pScnMgr->InstancesNum(); ++i)
  {
    const auto& info = m_pScnMgr->GetMeshInfo(m_pScnMgr->GetInstanceInfo(i).mesh_id);
    auto vertices = reinterpret_cast<float*>((char*)meshesData->VertexData() + info.m_vertexOffset * meshesData->SingleVertexSize());
    auto matrix = m_pScnMgr->GetInstanceMatrix(i);

    auto stride = meshesData->SingleVertexSize() / sizeof(float);
    for(size_t v = 0; v < info.m_vertNum; ++v)
    {
      sceneBbox.include(matrix * float4(vertices[v * stride + 0], vertices[v * stride + 1], vertices[v * stride + 2], 1.0f));
    }
  }
  sceneBbox.boxMin -= 1e-3f + VOXEL_SIZE * 0.5;
  sceneBbox.boxMax += 1e-3f + VOXEL_SIZE * 0.5;
}

void SimpleRender::RayTraceGPU()
{
  if(!m_pRayTracerGPU)
  {
    m_pRayTracerGPU = std::make_unique<RayTracer_GPU>(m_width, m_height);
    m_pRayTracerGPU->InitVulkanObjects(m_device, m_physicalDevice, m_width * m_height);
    m_pRayTracerGPU->InitMemberBuffers();

    const size_t bufferSize1 = m_width * m_height * sizeof(uint32_t);

    m_genColorBuffer = vk_utils::createBuffer(m_device, bufferSize1,  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    m_colorMem       = vk_utils::allocateAndBindWithPadding(m_device, m_physicalDevice, {m_genColorBuffer});

    auto tmp = std::make_shared<VulkanRTX>(m_pScnMgr);
    tmp->CommitScene();

    m_pRayTracerGPU->SetScene(tmp);
    m_pRayTracerGPU->SetVulkanInOutFor_CastSingleRay(m_genColorBuffer, 0);
    m_pRayTracerGPU->UpdateAll(m_pCopyHelper);
  }

  m_pRayTracerGPU->UpdateView(m_cam.pos, m_inverseProjViewMatrix);
  m_pRayTracerGPU->UpdatePlainMembers(m_pCopyHelper);
  
  // do ray tracing
  //
  {
    VkCommandBuffer commandBuffer = vk_utils::createCommandBuffer(m_device, m_commandPool);

    VkCommandBufferBeginInfo beginCommandBufferInfo = {};
    beginCommandBufferInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginCommandBufferInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginCommandBufferInfo);
    m_pRayTracerGPU->CastSingleRayCmd(commandBuffer, m_width, m_height, nullptr);
    
    // prepare buffer and image for copy command
    {
      VkBufferMemoryBarrier transferBuff = {};
      
      transferBuff.sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
      transferBuff.pNext               = nullptr;
      transferBuff.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      transferBuff.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      transferBuff.size                = VK_WHOLE_SIZE;
      transferBuff.offset              = 0;
      transferBuff.buffer              = m_genColorBuffer;
      transferBuff.srcAccessMask       = VK_ACCESS_SHADER_WRITE_BIT;
      transferBuff.dstAccessMask       = VK_ACCESS_TRANSFER_READ_BIT;

      VkImageMemoryBarrier transferImage;
      transferImage.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
      transferImage.pNext               = nullptr;
      transferImage.srcAccessMask       = 0;
      transferImage.dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
      transferImage.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
      transferImage.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; 
      transferImage.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      transferImage.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      transferImage.image               = m_rtImage.image;

      transferImage.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
      transferImage.subresourceRange.baseMipLevel   = 0;
      transferImage.subresourceRange.baseArrayLayer = 0;
      transferImage.subresourceRange.layerCount     = 1;
      transferImage.subresourceRange.levelCount     = 1;
    
      vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 1, &transferBuff, 1, &transferImage);
    }

    // execute copy
    //
    {
      VkImageSubresourceLayers subresourceLayers = {};
      subresourceLayers.aspectMask               = VK_IMAGE_ASPECT_COLOR_BIT;
      subresourceLayers.mipLevel                 = 0;
      subresourceLayers.baseArrayLayer           = 0;
      subresourceLayers.layerCount               = 1;

      VkBufferImageCopy copyRegion = {};
      copyRegion.bufferOffset      = 0;
      copyRegion.bufferRowLength   = uint32_t(m_width);
      copyRegion.bufferImageHeight = uint32_t(m_height);
      copyRegion.imageExtent       = VkExtent3D{ uint32_t(m_width), uint32_t(m_height), 1 };
      copyRegion.imageOffset       = VkOffset3D{ 0, 0, 0 };
      copyRegion.imageSubresource  = subresourceLayers;
  
      vkCmdCopyBufferToImage(commandBuffer, m_genColorBuffer, m_rtImage.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);
    }
    
    // get back normal image layout
    {
      VkImageMemoryBarrier transferImage;
      transferImage.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
      transferImage.pNext               = nullptr;
      transferImage.srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
      transferImage.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
      transferImage.oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
      transferImage.newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; 
      transferImage.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      transferImage.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      transferImage.image               = m_rtImage.image;

      transferImage.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
      transferImage.subresourceRange.baseMipLevel   = 0;
      transferImage.subresourceRange.baseArrayLayer = 0;
      transferImage.subresourceRange.layerCount     = 1;
      transferImage.subresourceRange.levelCount     = 1;
    
      vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &transferImage);
    }


    vkEndCommandBuffer(commandBuffer);

    vk_utils::executeCommandBufferNow(commandBuffer, m_graphicsQueue, m_device);
  }

}

void SimpleRender::TraceGenSamples()
{
  if(!m_pRayTracerGPU)
  {
    m_pRayTracerGPU = std::make_unique<RayTracer_GPU>(m_width, m_height);
    m_pRayTracerGPU->InitVulkanObjects(m_device, m_physicalDevice, m_width * m_height);
    m_pRayTracerGPU->InitMemberBuffers();

    const size_t bufferSize1 = m_width * m_height * sizeof(uint32_t);

    auto tmp = std::make_shared<VulkanRTX>(m_pScnMgr);
    tmp->CommitScene();

    m_pRayTracerGPU->SetScene(tmp);
    m_pRayTracerGPU->SetVulkanInOutForGenSamples(
      pointsBuffer, indirectPointsBuffer,
      samplePointsBuffer, m_pScnMgr->GetVertexBuffer(), m_pScnMgr->GetIndexBuffer(),
      m_pScnMgr->GetInstanceMatBuffer(), m_pScnMgr->GetMeshInfoBuffer(),
      primCounterBuffer, FFClusteredBuffer, initLightingBuffer, reflLightingBuffer,
      debugBuffer, debugIndirBuffer, nonEmptyVoxelsBuffer, indirVoxelsBuffer,
      appliedLightingBuffer, ffRowLenBuffer, ffTmpRowBuffer);
    m_pRayTracerGPU->UpdateAll(m_pCopyHelper);
  }

  m_pRayTracerGPU->UpdateView(m_cam.pos, m_inverseProjViewMatrix);
  m_pRayTracerGPU->UpdatePlainMembers(m_pCopyHelper);
  
  // do ray tracing
  //
  static bool inited = false;
  // if (!inited)
  {
    if (computeState.version == 0 && computeState.ff_out == 0 && computeState.ff_in == 0)
    {
      VkCommandBuffer commandBuffer = vk_utils::createCommandBuffer(m_device, m_commandPool);

      VkCommandBufferBeginInfo beginCommandBufferInfo = {};
      beginCommandBufferInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
      beginCommandBufferInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

      vkBeginCommandBuffer(commandBuffer, &beginCommandBufferInfo);
      vkCmdFillBuffer(commandBuffer, indirectPointsBuffer, 0, sizeof(uint32_t) * 4 * voxelsCount, 0);
      vkCmdFillBuffer(commandBuffer, debugIndirBuffer, 0, sizeof(uint32_t) * 4, 0);
      vkCmdFillBuffer(commandBuffer, primCounterBuffer, 0, sizeof(uint32_t) * trianglesCount, 0);
      vkCmdFillBuffer(commandBuffer, indirVoxelsBuffer, 0, sizeof(uint32_t) * 4 * 2, 0);
      vkCmdFillBuffer(commandBuffer, ffRowLenBuffer, 0, sizeof(uint32_t) * (clustersCount + 1), 0);
      m_pRayTracerGPU->GenSamplesCmd(commandBuffer, PER_SURFACE_POINTS,
        to_float3(sceneBbox.boxMin), to_float3(sceneBbox.boxMax), VOXEL_SIZE, m_uniforms.time, m_pScnMgr->GetInstanceMatrix(0),
        maxPointsCount);

      vkEndCommandBuffer(commandBuffer);

      vk_utils::executeCommandBufferNow(commandBuffer, m_graphicsQueue, m_device);
    }
    if (!inited)
    {
      m_pCopyHelper->ReadBuffer(indirVoxelsBuffer, 0, &visibleVoxelsCount, sizeof(visibleVoxelsCount));
      std::cout << "Visible voxels count " << visibleVoxelsCount << std::endl;
      assert(visibleVoxelsApproxCount >= visibleVoxelsCount);
    }
    {
      VkCommandBuffer commandBuffer = vk_utils::createCommandBuffer(m_device, m_commandPool);

      VkCommandBufferBeginInfo beginCommandBufferInfo = {};
      beginCommandBufferInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
      beginCommandBufferInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

      vkBeginCommandBuffer(commandBuffer, &beginCommandBufferInfo);
      if (computeState.version == 0 && computeState.ff_out == 0 && computeState.ff_in == 0)
        vkCmdFillBuffer(commandBuffer, FFClusteredBuffer, 0, 2 * sizeof(float) * approxColumns * clustersCount, 0);
      if (computeState.ff_out == 0 && computeState.ff_in == 0)
        vkCmdFillBuffer(commandBuffer, ffRowLenBuffer, 0, sizeof(uint32_t) * clustersCount, 0);
      vkCmdFillBuffer(commandBuffer, appliedLightingBuffer, 0, sizeof(float) * voxelsCount * 6, 0);
      m_pRayTracerGPU->ComputeFFCmd(commandBuffer, PER_SURFACE_POINTS, visibleVoxelsCount, computeState.ff_out);
      m_pRayTracerGPU->packFFCmd(commandBuffer, PER_SURFACE_POINTS, visibleVoxelsCount, computeState.ff_out);
      // if (computeState.ff_out + 1 == visibleVoxelsCount && computeState.ff_in + FF_UPDATE_COUNT >= visibleVoxelsCount)
      //   m_pRayTracerGPU->CorrectFFCmd(commandBuffer, visibleVoxelsCount);
      m_pRayTracerGPU->initLightingCmd(commandBuffer, visibleVoxelsCount, VOXEL_SIZE,
        to_float3(sceneBbox.boxMin), to_float3(sceneBbox.boxMax), to_float3(m_uniforms.lightPos), PER_VOXEL_POINTS);
      m_pRayTracerGPU->reflLightingCmd(commandBuffer, visibleVoxelsCount);
      m_pRayTracerGPU->finalLightingCmd(commandBuffer, visibleVoxelsCount);

      vkEndCommandBuffer(commandBuffer);

      vk_utils::executeCommandBufferNow(commandBuffer, m_graphicsQueue, m_device);
    }
    inited = true;
  }
  computeState.ff_in += FF_UPDATE_COUNT;
  if (computeState.ff_in >= visibleVoxelsCount)
  {
    computeState.ff_in = 0;
    computeState.ff_out++;
  }
  if (computeState.ff_out == visibleVoxelsCount)
  {
    computeState.ff_out = 0;
    computeState.version++;
  }
  if (computeState.ff_out == 0 && computeState.ff_in == 0 && computeState.version == 1)
  {
    std::vector<uint32_t> rowLens(visibleVoxelsCount * 6 + 1);
    m_pCopyHelper->ReadBuffer(ffRowLenBuffer, 0, rowLens.data(), sizeof(rowLens[0]) * rowLens.size());
    std::cout << "FF total count:" << rowLens.back() << std::endl;
    assert(rowLens.size() <= approxColumns * clustersCount);
  }
}
