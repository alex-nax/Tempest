#if defined(TEMPEST_BUILD_VULKAN)

#include "vcommandbuffer.h"

#include <Tempest/Attachment>

#include "vdevice.h"
#include "vcommandpool.h"
#include "vpipeline.h"
#include "vbuffer.h"
#include "vdescriptorarray.h"
#include "vswapchain.h"
#include "vtexture.h"
#include "vframebuffermap.h"

using namespace Tempest;
using namespace Tempest::Detail;

static VkAttachmentLoadOp mkLoadOp(const AccessOp op) {
  switch (op) {
    case AccessOp::Discard:
      return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    case AccessOp::Preserve:
      return VK_ATTACHMENT_LOAD_OP_LOAD;
    case AccessOp::Clear:
      return VK_ATTACHMENT_LOAD_OP_CLEAR;
    }
  return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  }

static VkAttachmentStoreOp mkStoreOp(const AccessOp op) {
  switch (op) {
    case AccessOp::Discard:
      return VK_ATTACHMENT_STORE_OP_DONT_CARE;
    case AccessOp::Preserve:
      return VK_ATTACHMENT_STORE_OP_STORE;
    case AccessOp::Clear:
      return VK_ATTACHMENT_STORE_OP_STORE;
    }
  return VK_ATTACHMENT_STORE_OP_DONT_CARE;
  }

static VkAccessFlagBits nativeFormat(ResourceAccess rs) {
  uint32_t ret = 0;
  if((rs&ResourceAccess::TransferSrc)==ResourceAccess::TransferSrc)
    ret |= VK_ACCESS_TRANSFER_READ_BIT;
  if((rs&ResourceAccess::TransferDst)==ResourceAccess::TransferDst)
    ret |= VK_ACCESS_TRANSFER_WRITE_BIT;

  if((rs&ResourceAccess::Present)==ResourceAccess::Present)
    ret |= VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;

  if((rs&ResourceAccess::Sampler)==ResourceAccess::Sampler)
    ret |= VK_ACCESS_SHADER_READ_BIT;
  if((rs&ResourceAccess::ColorAttach)==ResourceAccess::ColorAttach)
    ret |= (VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
  if((rs&ResourceAccess::DepthAttach)==ResourceAccess::DepthAttach)
    ret |= (VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
  if((rs&ResourceAccess::Unordered)==ResourceAccess::Unordered)
    ret |= (VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);

  if((rs&ResourceAccess::Index)==ResourceAccess::Index)
    ret |= VK_ACCESS_INDEX_READ_BIT;
  if((rs&ResourceAccess::Vertex)==ResourceAccess::Vertex)
    ret |= VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
  if((rs&ResourceAccess::Vertex)==ResourceAccess::Uniform)
    ret |= VK_ACCESS_UNIFORM_READ_BIT;

  if((rs&ResourceAccess::ComputeRead)==ResourceAccess::ComputeRead)
    ret |= VK_ACCESS_SHADER_READ_BIT;
  if((rs&ResourceAccess::ComputeWrite)==ResourceAccess::ComputeWrite)
    ret |= VK_ACCESS_SHADER_WRITE_BIT;

  return VkAccessFlagBits(ret);
  }

static VkPipelineStageFlags toStage(ResourceAccess rs, bool isSrc) {
  uint32_t ret = 0;
  if((rs&ResourceAccess::TransferSrc)==ResourceAccess::TransferSrc)
    ret |= VK_PIPELINE_STAGE_TRANSFER_BIT;
  if((rs&ResourceAccess::TransferDst)==ResourceAccess::TransferDst)
    ret |= VK_PIPELINE_STAGE_TRANSFER_BIT;

  if((rs&ResourceAccess::Present)==ResourceAccess::Present)
    ret |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

  if((rs&ResourceAccess::Sampler)==ResourceAccess::Sampler)
    ret |= VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
  if((rs&ResourceAccess::ColorAttach)==ResourceAccess::ColorAttach)
    ret |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  if((rs&ResourceAccess::DepthAttach)==ResourceAccess::DepthAttach)
    ret |= (VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT);
  if((rs&ResourceAccess::Unordered)==ResourceAccess::Unordered)
    ret |= VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;

  if((rs&ResourceAccess::Index)==ResourceAccess::Index)
    ret |= VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
  if((rs&ResourceAccess::Vertex)==ResourceAccess::Vertex)
    ret |= VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
  if((rs&ResourceAccess::Vertex)==ResourceAccess::Uniform)
    ret |= VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;

  if((rs&ResourceAccess::ComputeRead)==ResourceAccess::ComputeRead)
    ret |= VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
  if((rs&ResourceAccess::ComputeWrite)==ResourceAccess::ComputeWrite)
    ret |= VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
  if(isSrc && ret==0)
    ret = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT; // wait for nothing: asset uploading case
  return VkPipelineStageFlagBits(ret);
  }

static VkImageLayout toLayout(ResourceAccess rs) {
  if((rs&ResourceAccess::TransferSrc)==ResourceAccess::TransferSrc)
    return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  if((rs&ResourceAccess::TransferDst)==ResourceAccess::TransferDst)
    return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

  if((rs&ResourceAccess::Present)==ResourceAccess::Present)
    return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

  if((rs&ResourceAccess::Sampler)==ResourceAccess::Sampler)
    return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  if((rs&ResourceAccess::ColorAttach)==ResourceAccess::ColorAttach)
    return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  if((rs&ResourceAccess::DepthAttach)==ResourceAccess::DepthAttach)
    return VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
  if((rs&ResourceAccess::Unordered)==ResourceAccess::Unordered)
    return VK_IMAGE_LAYOUT_GENERAL;

  if((rs&ResourceAccess::Index)==ResourceAccess::Index)
    return VK_IMAGE_LAYOUT_GENERAL;
  if((rs&ResourceAccess::Vertex)==ResourceAccess::Vertex)
    return VK_IMAGE_LAYOUT_GENERAL;
  if((rs&ResourceAccess::Vertex)==ResourceAccess::Uniform)
    return VK_IMAGE_LAYOUT_GENERAL;

  if((rs&ResourceAccess::ComputeRead)==ResourceAccess::ComputeRead)
    return VK_IMAGE_LAYOUT_GENERAL;
  if((rs&ResourceAccess::ComputeWrite)==ResourceAccess::ComputeWrite)
    return VK_IMAGE_LAYOUT_GENERAL;

  return VK_IMAGE_LAYOUT_UNDEFINED;
  }

static VkImage toVkResource(const AbstractGraphicsApi::BarrierDesc& b) {
  if(b.texture!=nullptr) {
    VTexture& t = *reinterpret_cast<VTexture*>(b.texture);
    return t.impl;
    }

  if(b.buffer!=nullptr) {
    VBuffer& buf = *reinterpret_cast<VBuffer*>(b.buffer);
    return buf.impl;
    }

  VSwapchain& s = *reinterpret_cast<VSwapchain*>(b.swapchain);
  return s.images[b.swId];
  }


VCommandBuffer::VCommandBuffer(VDevice& device, VkCommandPoolCreateFlags flags)
  :device(device), pool(device,flags) {
  VkCommandBufferAllocateInfo allocInfo = {};
  allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.commandPool        = pool.impl;
  allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandBufferCount = 1;

  vkAssert(vkAllocateCommandBuffers(device.device.impl,&allocInfo,&impl));
  }

VCommandBuffer::~VCommandBuffer() {
  vkFreeCommandBuffers(device.device.impl,pool.impl,1,&impl);
  }

void VCommandBuffer::reset() {
  vkResetCommandBuffer(impl,VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT);
  swapchainSync.reserve(swapchainSync.size());
  swapchainSync.clear();
  }

void VCommandBuffer::begin() {
  begin(0);
  }

void VCommandBuffer::begin(VkCommandBufferUsageFlags flg) {
  state     = Idle;
  swapchainSync.reserve(swapchainSync.size());
  swapchainSync.clear();
  curVbo = VK_NULL_HANDLE;

  VkCommandBufferBeginInfo beginInfo = {};
  beginInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags            = flg;
  beginInfo.pInheritanceInfo = nullptr;

  vkAssert(vkBeginCommandBuffer(impl,&beginInfo));
  }

void VCommandBuffer::end() {
  swapchainSync.reserve(swapchainSync.size());
  resState.finalize(*this);
  vkAssert(vkEndCommandBuffer(impl));
  state = NoRecording;
  }

bool VCommandBuffer::isRecording() const {
  return state!=NoRecording;
  }

void VCommandBuffer::beginRendering(const AttachmentDesc* desc, size_t descSize,
                                    uint32_t width, uint32_t height,
                                    const TextureFormat* frm,
                                    AbstractGraphicsApi::Texture** att,
                                    AbstractGraphicsApi::Swapchain** sw, const uint32_t* imgId) {
  for(size_t i=0; i<descSize; ++i) {
    if(sw[i]!=nullptr)
      addDependency(*reinterpret_cast<VSwapchain*>(sw[i]),imgId[i]);
    }

  resState.joinCompute();
  resState.setRenderpass(*this,desc,descSize,frm,att,sw,imgId);

  if(device.props.hasDynRendering) {
    VkRenderingAttachmentInfoKHR colorAtt[MaxFramebufferAttachments] = {};
    VkRenderingAttachmentInfoKHR depthAtt = {};

    VkRenderingInfoKHR info = {};
    info.sType                = VK_STRUCTURE_TYPE_RENDERING_INFO_KHR;
    info.flags                = 0;
    info.renderArea.offset    = {0, 0};
    info.renderArea.extent    = {width,height};
    info.layerCount           = 1;
    info.viewMask             = 0;
    info.colorAttachmentCount = 0;
    info.pColorAttachments    = colorAtt;

    passDyn.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
    passDyn.pNext                   = nullptr;
    passDyn.viewMask                = info.viewMask;
    passDyn.colorAttachmentCount    = 0;
    passDyn.pColorAttachmentFormats = passDyn.colorFrm;

    VkImageView imageView   = VK_NULL_HANDLE;
    VkFormat    imageFormat = VK_FORMAT_UNDEFINED;
    for(size_t i = 0; i < descSize; ++i) {
      if(sw[i] != nullptr) {
        auto& t = *reinterpret_cast<VSwapchain*>(sw[i]);
        imageView   = t.views[imgId[i]];
        imageFormat = t.format();
        }
      else if (desc[i].attachment != nullptr) {
        auto& t = *reinterpret_cast<VTexture*>(att[i]);
        imageView   = t.imgView;
        imageFormat = t.format;
        }
      else {
        auto &t = *reinterpret_cast<VTexture*>(att[i]);
        imageView   = t.imgView;
        imageFormat = t.format;
        }

      auto& att = isDepthFormat(frm[i]) ? depthAtt : colorAtt[info.colorAttachmentCount];
      att.sType     = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR;
      att.imageView = imageView;

      if(isDepthFormat(frm[i])) {
        info.pDepthAttachment = &depthAtt;
        att.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        passDyn.depthAttachmentFormat = imageFormat;
        auto& clr = att.clearValue;
        clr.depthStencil.depth = desc[i].clear.x;
        } else {
        passDyn.colorFrm[info.colorAttachmentCount] = imageFormat;
        ++info.colorAttachmentCount;

        att.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        auto& clr = att.clearValue;
        clr.color.float32[0] = desc[i].clear.x;
        clr.color.float32[1] = desc[i].clear.y;
        clr.color.float32[2] = desc[i].clear.z;
        clr.color.float32[3] = desc[i].clear.w;
        }

      att.resolveMode        = VK_RESOLVE_MODE_NONE;
      att.resolveImageView   = VK_NULL_HANDLE;
      att.resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      att.loadOp             = mkLoadOp(desc[i].load);
      att.storeOp            = mkStoreOp(desc[i].store);
      }
    passDyn.colorAttachmentCount = info.colorAttachmentCount;

    device.vkCmdBeginRenderingKHR(impl,&info);
    } else {
    auto fbo = device.fboMap.find(desc,descSize, att,sw,imgId,width,height);
    auto fb  = fbo.get();
    pass = fbo->pass;

    VkClearValue clr[MaxFramebufferAttachments];
    for(size_t i=0; i<descSize; ++i) {
      if(isDepthFormat(frm[i])) {
        clr[i].depthStencil.depth = desc[i].clear.x;
        } else {
        clr[i].color.float32[0]   = desc[i].clear.x;
        clr[i].color.float32[1]   = desc[i].clear.y;
        clr[i].color.float32[2]   = desc[i].clear.z;
        clr[i].color.float32[3]   = desc[i].clear.w;
        }
      }

    VkRenderPassBeginInfo info = {};
    info.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    info.renderPass        = fb->pass->pass;
    info.framebuffer       = fb->fbo;
    info.renderArea.offset = {0, 0};
    info.renderArea.extent = {width,height};

    info.clearValueCount   = uint32_t(descSize);
    info.pClearValues      = clr;

    vkCmdBeginRenderPass(impl, &info, VK_SUBPASS_CONTENTS_INLINE);
    }
  state = RenderPass;

  // setup dynamic state
  // https://www.khronos.org/registry/vulkan/specs/1.1-extensions/html/vkspec.html#pipelines-dynamic-state
  setViewport(Rect(0,0,int32_t(width),int32_t(height)));

  VkRect2D scissor = {};
  scissor.offset = {0, 0};
  scissor.extent = {width, height};
  vkCmdSetScissor(impl,0,1,&scissor);
  }

void VCommandBuffer::endRendering() {
  if(device.props.hasDynRendering) {
    device.vkCmdEndRenderingKHR(impl);
    } else {
    vkCmdEndRenderPass(impl);
    }
  state     = Idle;
  }

void VCommandBuffer::setPipeline(AbstractGraphicsApi::Pipeline& p) {
  VPipeline& px = reinterpret_cast<VPipeline&>(p);
  auto&      v  = device.props.hasDynRendering ? px.instance(passDyn) : px.instance(pass);
  vkCmdBindPipeline(impl,VK_PIPELINE_BIND_POINT_GRAPHICS,v.val);
  ssboBarriers = px.ssboBarriers;
  }

void VCommandBuffer::setBytes(AbstractGraphicsApi::Pipeline& p, const void* data, size_t size) {
  VPipeline&        px=reinterpret_cast<VPipeline&>(p);
  assert(size<=px.pushSize);
  vkCmdPushConstants(impl, px.pipelineLayout, px.pushStageFlags, 0, uint32_t(size), data);
  }

void VCommandBuffer::setUniforms(AbstractGraphicsApi::Pipeline &p, AbstractGraphicsApi::Desc &u) {
  VPipeline&        px=reinterpret_cast<VPipeline&>(p);
  VDescriptorArray& ux=reinterpret_cast<VDescriptorArray&>(u);
  curUniforms = &ux;
  vkCmdBindDescriptorSets(impl,VK_PIPELINE_BIND_POINT_GRAPHICS,
                          px.pipelineLayout,0,
                          1,&ux.desc,
                          0,nullptr);
  }

void VCommandBuffer::setComputePipeline(AbstractGraphicsApi::CompPipeline& p) {
  if(state!=Compute) {
    state = Compute;
    }
  VCompPipeline& px = reinterpret_cast<VCompPipeline&>(p);
  vkCmdBindPipeline(impl,VK_PIPELINE_BIND_POINT_COMPUTE,px.impl);
  ssboBarriers = px.ssboBarriers;
  }

void VCommandBuffer::setBytes(AbstractGraphicsApi::CompPipeline& p, const void* data, size_t size) {
  VCompPipeline& px=reinterpret_cast<VCompPipeline&>(p);
  assert(size<=px.pushSize);
  vkCmdPushConstants(impl, px.pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, uint32_t(size), data);
  }

void VCommandBuffer::setUniforms(AbstractGraphicsApi::CompPipeline& p, AbstractGraphicsApi::Desc& u) {
  VCompPipeline&    px=reinterpret_cast<VCompPipeline&>(p);
  VDescriptorArray& ux=reinterpret_cast<VDescriptorArray&>(u);
  curUniforms = &ux;
  vkCmdBindDescriptorSets(impl,VK_PIPELINE_BIND_POINT_COMPUTE,
                          px.pipelineLayout,0,
                          1,&ux.desc,
                          0,nullptr);
  }

void VCommandBuffer::draw(const AbstractGraphicsApi::Buffer& ivbo, size_t offset, size_t size, size_t firstInstance, size_t instanceCount) {
  if(T_UNLIKELY(ssboBarriers)) {
    curUniforms->ssboBarriers(resState);
    }
  const VBuffer& vbo=reinterpret_cast<const VBuffer&>(ivbo);
  if(curVbo!=vbo.impl) {
    VkBuffer     buffers[1] = {vbo.impl};
    VkDeviceSize offsets[1] = {0};
    vkCmdBindVertexBuffers(impl, 0, 1, buffers, offsets );
    curVbo = vbo.impl;
    }
  vkCmdDraw(impl, uint32_t(size), uint32_t(instanceCount), uint32_t(offset), uint32_t(firstInstance));
  }

void VCommandBuffer::drawIndexed(const AbstractGraphicsApi::Buffer& ivbo, const AbstractGraphicsApi::Buffer& iibo, Detail::IndexClass cls,
                                 size_t ioffset, size_t isize, size_t voffset, size_t firstInstance, size_t instanceCount) {
  if(T_UNLIKELY(ssboBarriers)) {
    curUniforms->ssboBarriers(resState);
    }
  static const VkIndexType type[]={
    VK_INDEX_TYPE_UINT16,
    VK_INDEX_TYPE_UINT32
    };

  const VBuffer& vbo = reinterpret_cast<const VBuffer&>(ivbo);
  const VBuffer& ibo = reinterpret_cast<const VBuffer&>(iibo);
  if(curVbo!=vbo.impl) {
    VkBuffer     buffers[1] = {vbo.impl};
    VkDeviceSize offsets[1] = {0};
    vkCmdBindVertexBuffers(impl, 0, 1, buffers, offsets );
    curVbo = vbo.impl;
    }
  vkCmdBindIndexBuffer(impl, ibo.impl, 0, type[uint32_t(cls)]);
  vkCmdDrawIndexed    (impl, uint32_t(isize), uint32_t(instanceCount), uint32_t(ioffset), int32_t(voffset), uint32_t(firstInstance));
  }

void VCommandBuffer::dispatch(size_t x, size_t y, size_t z) {
  if(T_UNLIKELY(ssboBarriers)) {
    curUniforms->ssboBarriers(resState);
    resState.flush(*this);
    }
  vkCmdDispatch(impl,uint32_t(x),uint32_t(y),uint32_t(z));
  }

void VCommandBuffer::setViewport(const Tempest::Rect &r) {
  VkViewport viewPort = {};
  viewPort.x        = float(r.x);
  viewPort.y        = float(r.y);
  viewPort.width    = float(r.w);
  viewPort.height   = float(r.h);
  viewPort.minDepth = 0;
  viewPort.maxDepth = 1;

  vkCmdSetViewport(impl,0,1,&viewPort);
  }

void VCommandBuffer::setScissor(const Rect& r) {
  VkRect2D scissor = {};
  scissor.offset = {r.x, r.y};
  scissor.extent = {uint32_t(r.w), uint32_t(r.h)};
  vkCmdSetScissor(impl,0,1,&scissor);
  }

void VCommandBuffer::copy(AbstractGraphicsApi::Buffer& dstBuf, size_t offsetDest, const AbstractGraphicsApi::Buffer &srcBuf, size_t offsetSrc, size_t size) {
  auto& src = reinterpret_cast<const VBuffer&>(srcBuf);
  auto& dst = reinterpret_cast<VBuffer&>(dstBuf);

  VkBufferCopy copyRegion = {};
  copyRegion.dstOffset = offsetDest;
  copyRegion.srcOffset = offsetSrc;
  copyRegion.size      = size;
  vkCmdCopyBuffer(impl, src.impl, dst.impl, 1, &copyRegion);
  }

void VCommandBuffer::copy(AbstractGraphicsApi::Buffer& dstBuf, size_t offsetDest, const void* src, size_t size) {
  auto&  dst    = reinterpret_cast<VBuffer&>(dstBuf);

  auto   srcBuf = reinterpret_cast<const uint8_t*>(src);
  size_t maxSz  = 0x10000;
  for(size_t at=0; at<size; ) {
    const size_t sz = std::min(size-at, maxSz);
    vkCmdUpdateBuffer(impl,dst.impl,offsetDest+at,sz,srcBuf);
    srcBuf+=sz;
    at    +=sz;
    }
  }

void VCommandBuffer::copy(AbstractGraphicsApi::Texture& dstTex, size_t width, size_t height, size_t mip, const AbstractGraphicsApi::Buffer& srcBuf, size_t offset) {
  auto& src = reinterpret_cast<const VBuffer&>(srcBuf);
  auto& dst = reinterpret_cast<VTexture&>(dstTex);

  VkBufferImageCopy region = {};
  region.bufferOffset      = offset;
  region.bufferRowLength   = 0;
  region.bufferImageHeight = 0;
  region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  region.imageSubresource.mipLevel = uint32_t(mip);
  region.imageSubresource.baseArrayLayer = 0;
  region.imageSubresource.layerCount = 1;
  region.imageOffset = {0, 0, 0};
  region.imageExtent = {
      uint32_t(width),
      uint32_t(height),
      1
  };

  vkCmdCopyBufferToImage(impl, src.impl, dst.impl, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
  }

void VCommandBuffer::copyNative(AbstractGraphicsApi::Buffer&        dst, size_t offset,
                                const AbstractGraphicsApi::Texture& src, size_t width, size_t height, size_t mip) {
  auto& nSrc = reinterpret_cast<const VTexture&>(src);
  auto& nDst = reinterpret_cast<VBuffer&>(dst);

  VkBufferImageCopy region={};
  region.bufferOffset      = offset;
  region.bufferRowLength   = 0;
  region.bufferImageHeight = 0;
  region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  region.imageSubresource.mipLevel = uint32_t(mip);
  region.imageSubresource.baseArrayLayer = 0;
  region.imageSubresource.layerCount = 1;
  region.imageOffset = {0, 0, 0};
  region.imageExtent = {
      uint32_t(width),
      uint32_t(height),
      1
  };

  vkCmdCopyImageToBuffer(impl, nSrc.impl, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, nDst.impl, 1, &region);
  }

void VCommandBuffer::blit(AbstractGraphicsApi::Texture& srcTex, uint32_t srcW, uint32_t srcH, uint32_t srcMip,
                          AbstractGraphicsApi::Texture& dstTex, uint32_t dstW, uint32_t dstH, uint32_t dstMip) {
  auto& src = reinterpret_cast<VTexture&>(srcTex);
  auto& dst = reinterpret_cast<VTexture&>(dstTex);

  // Check if image format supports linear blitting
  VkFormatProperties formatProperties;
  vkGetPhysicalDeviceFormatProperties(device.physicalDevice, src.format, &formatProperties);

  VkFilter filter = VK_FILTER_LINEAR;
  if(0==(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) {
    filter = VK_FILTER_NEAREST;
    }

  VkImageBlit blit = {};
  blit.srcOffsets[0] = {0, 0, 0};
  blit.srcOffsets[1] = {int32_t(srcW), int32_t(srcH), 1};
  blit.srcSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
  blit.srcSubresource.mipLevel       = srcMip;
  blit.srcSubresource.baseArrayLayer = 0;
  blit.srcSubresource.layerCount     = 1;
  blit.dstOffsets[0] = {0, 0, 0};
  blit.dstOffsets[1] = {int32_t(dstW), int32_t(dstH), 1 };
  blit.dstSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
  blit.dstSubresource.mipLevel       = dstMip;
  blit.dstSubresource.baseArrayLayer = 0;
  blit.dstSubresource.layerCount     = 1;

  vkCmdBlitImage(impl,
                 src.impl, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                 dst.impl, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                 1, &blit,
                 filter);
  }

void VCommandBuffer::copy(AbstractGraphicsApi::Buffer& dst, size_t offset,
                          AbstractGraphicsApi::Texture& src, uint32_t width, uint32_t height, uint32_t mip) {
  resState.setLayout(dst,ResourceAccess::TransferDst);
  resState.setLayout(src,ResourceAccess::TransferSrc);
  resState.flush(*this);

  copyNative(dst,offset, src,width,height,mip);

  resState.setLayout(dst,ResourceAccess::ComputeRead);
  resState.setLayout(src,ResourceAccess::Sampler); // TODO: storage images
  }

void VCommandBuffer::generateMipmap(AbstractGraphicsApi::Texture& img,
                                    uint32_t texWidth, uint32_t texHeight, uint32_t mipLevels) {
  if(mipLevels==1)
    return;

  auto& image = reinterpret_cast<VTexture&>(img);

  // Check if image format supports linear blitting
  VkFormatProperties formatProperties;
  vkGetPhysicalDeviceFormatProperties(device.physicalDevice, image.format, &formatProperties);

  if(!(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT))
    throw std::runtime_error("texture image format does not support linear blitting!");

  int32_t w = int32_t(texWidth);
  int32_t h = int32_t(texHeight);

  resState.setLayout(img,ResourceAccess::TransferDst);
  resState.flush(*this);

  for(uint32_t i=1; i<mipLevels; ++i) {
    const int mw = (w==1 ? 1 : w/2);
    const int mh = (h==1 ? 1 : h/2);

    barrier(img,ResourceAccess::TransferDst, ResourceAccess::TransferSrc,i-1);
    blit(img,  w, h, i-1,
         img, mw,mh, i);

    w = mw;
    h = mh;
    }
  barrier(img,ResourceAccess::TransferDst, ResourceAccess::TransferSrc, mipLevels-1);
  barrier(img,ResourceAccess::TransferSrc, ResourceAccess::Sampler,     uint32_t(-1));
  }

void VCommandBuffer::barrier(const AbstractGraphicsApi::BarrierDesc* desc, size_t cnt) {
  if(device.props.hasSync2) {
    barrier2(desc,cnt);
    return;
    }

  {
  VkBufferMemoryBarrier bufBarrier[MaxBarriers] = {};
  VkPipelineStageFlags  srcStage = 0;
  VkPipelineStageFlags  dstStage = 0;
  uint32_t              pbCount  = 0;

  for(size_t i=0; i<cnt; ++i) {
    auto& b = desc[i];
    if(b.buffer==nullptr)
      continue;
    auto nSrcStage = toStage(b.prev,true);
    auto nDstStage = toStage(b.next,false);

    if(nSrcStage!=srcStage || nDstStage!=dstStage) {
      emitBarriers(srcStage,dstStage,bufBarrier,pbCount);
      srcStage = nSrcStage;
      dstStage = nDstStage;
      pbCount  = 0;
      }

    auto& bx = bufBarrier[pbCount];
    bx.sType                 = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    bx.srcQueueFamilyIndex   = VK_QUEUE_FAMILY_IGNORED;
    bx.dstQueueFamilyIndex   = VK_QUEUE_FAMILY_IGNORED;
    bx.buffer                = reinterpret_cast<VBuffer&>(*b.buffer).impl;
    bx.offset                = 0;
    bx.size                  = VK_WHOLE_SIZE;

    bx.srcAccessMask         = ::nativeFormat(b.prev);
    bx.dstAccessMask         = ::nativeFormat(b.next);

    ++pbCount;
    }
  emitBarriers(srcStage,dstStage,bufBarrier,pbCount);
  }

  {
  VkImageMemoryBarrier imgBarrier[MaxBarriers] = {};
  VkPipelineStageFlags srcStage = 0;
  VkPipelineStageFlags dstStage = 0;
  uint32_t             pbCount  = 0;

  for(size_t i=0; i<cnt; ++i) {
    auto& b = desc[i];
    if(b.buffer!=nullptr)
      continue;

    auto nSrcStage = toStage(b.prev,true);
    auto nDstStage = toStage(b.next,false);

    if(nSrcStage!=srcStage || nDstStage!=dstStage) {
      emitBarriers(srcStage,dstStage,imgBarrier,pbCount);
      srcStage = nSrcStage;
      dstStage = nDstStage;
      pbCount  = 0;
      }

    auto& bx = imgBarrier[pbCount];
    bx.sType                 = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    bx.srcQueueFamilyIndex   = VK_QUEUE_FAMILY_IGNORED;
    bx.dstQueueFamilyIndex   = VK_QUEUE_FAMILY_IGNORED;
    bx.image                 = toVkResource(b);

    bx.srcAccessMask         = ::nativeFormat(b.prev);
    bx.dstAccessMask         = ::nativeFormat(b.next);

    bx.oldLayout             = toLayout(b.prev);
    bx.newLayout             = toLayout(b.next);
    finalizeImageBarrier(bx,b);
    ++pbCount;
    }
  emitBarriers(srcStage,dstStage,imgBarrier,pbCount);
  }
  }

void VCommandBuffer::barrier2(const AbstractGraphicsApi::BarrierDesc* desc, size_t cnt) {
  VkBufferMemoryBarrier2KHR bufBarrier[MaxBarriers] = {};
  uint32_t                  bufCount = 0;
  VkImageMemoryBarrier2KHR  imgBarrier[MaxBarriers] = {};
  uint32_t                  imgCount = 0;

  VkDependencyInfoKHR info = {};
  info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR;

  for(size_t i=0; i<cnt; ++i) {
    auto& b  = desc[i];

    if(b.buffer!=nullptr) {
      auto& bx = bufBarrier[bufCount];
      ++bufCount;

      bx.sType                 = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2_KHR;
      bx.srcQueueFamilyIndex   = VK_QUEUE_FAMILY_IGNORED;
      bx.dstQueueFamilyIndex   = VK_QUEUE_FAMILY_IGNORED;
      bx.buffer                = reinterpret_cast<VBuffer&>(*b.buffer).impl;
      bx.offset                = 0;
      bx.size                  = VK_WHOLE_SIZE;

      bx.srcAccessMask         = ::nativeFormat(b.prev);
      bx.dstAccessMask         = ::nativeFormat(b.next);
      bx.srcStageMask          = toStage(b.prev,true);
      bx.dstStageMask          = toStage(b.next,false);
      } else {
      auto& bx = imgBarrier[imgCount];
      ++imgCount;

      bx.sType                 = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2_KHR;
      bx.srcQueueFamilyIndex   = VK_QUEUE_FAMILY_IGNORED;
      bx.dstQueueFamilyIndex   = VK_QUEUE_FAMILY_IGNORED;
      bx.image                 = toVkResource(b);

      bx.srcAccessMask         = ::nativeFormat(b.prev);
      bx.dstAccessMask         = ::nativeFormat(b.next);
      bx.srcStageMask          = toStage(b.prev,true);
      bx.dstStageMask          = toStage(b.next,false);

      bx.oldLayout             = toLayout(b.prev);
      bx.newLayout             = toLayout(b.next);
      finalizeImageBarrier(bx,b);
      }
    }

  info.pBufferMemoryBarriers    = bufBarrier;
  info.bufferMemoryBarrierCount = bufCount;
  info.pImageMemoryBarriers     = imgBarrier;
  info.imageMemoryBarrierCount  = imgCount;

  device.vkCmdPipelineBarrier2KHR(impl,&info);
  }

template<class T>
void VCommandBuffer::finalizeImageBarrier(T& bx, const AbstractGraphicsApi::BarrierDesc& b) {
  if(b.prev==ResourceAccess::Present)
    bx.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  if(b.next==ResourceAccess::Present)
    bx.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

  VkFormat nativeFormat = VK_FORMAT_UNDEFINED;
  if(b.texture!=nullptr) {
    VTexture& t   = *reinterpret_cast<VTexture*>  (b.texture);
    nativeFormat  = t.format;
    } else {
    VSwapchain& s = *reinterpret_cast<VSwapchain*>(b.swapchain);
    nativeFormat  = s.format();
    }

  bx.subresourceRange.baseMipLevel   = b.mip==uint32_t(-1) ? 0 : b.mip;
  bx.subresourceRange.levelCount     = b.mip==uint32_t(-1) ? VK_REMAINING_MIP_LEVELS : 1;
  bx.subresourceRange.baseArrayLayer = 0;
  bx.subresourceRange.layerCount     = VK_REMAINING_ARRAY_LAYERS;

  if(nativeFormat==VK_FORMAT_D24_UNORM_S8_UINT || nativeFormat==VK_FORMAT_D16_UNORM_S8_UINT || nativeFormat==VK_FORMAT_D32_SFLOAT_S8_UINT)
    bx.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT; else
  if(Detail::nativeIsDepthFormat(nativeFormat))
    bx.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT; else
    bx.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

  if(b.discard)
    bx.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  }

void VCommandBuffer::addDependency(VSwapchain& s, size_t imgId) {
  VSwapchain::Sync* sc = nullptr;
  for(auto& i:s.sync)
    if(i.imgId==imgId) {
      sc = &i;
      break;
      }

  for(auto i:swapchainSync)
    if(i==sc)
      return;
  assert(sc!=nullptr);
  swapchainSync.push_back(sc);
  }

void VCommandBuffer::emitBarriers(VkPipelineStageFlags src, VkPipelineStageFlags dst,
                                  const VkBufferMemoryBarrier* b, uint32_t cnt) {
  if(cnt==0)
    return;
  vkCmdPipelineBarrier(
        impl,
        src, dst,
        0,
        0, nullptr,
        cnt, b,
        0, nullptr);
  }

void VCommandBuffer::emitBarriers(VkPipelineStageFlags src, VkPipelineStageFlags dst,
                                  const VkImageMemoryBarrier* b, uint32_t cnt) {
  if(cnt==0)
    return;
  vkCmdPipelineBarrier(
        impl,
        src, dst,
        0,
        0, nullptr,
        0, nullptr,
        cnt, b);
  }

#endif
