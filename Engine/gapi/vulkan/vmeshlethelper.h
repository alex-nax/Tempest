#pragma once

#include <Tempest/AbstractGraphicsApi>
#include <Tempest/PipelineLayout>

#include "vbuffer.h"

namespace Tempest {
namespace Detail {

class VDevice;
class VPipelineLay;
class VCompPipeline;

class VMeshletHelper {
  private:
    struct VkDrawIndexedIndirectCommand {
      uint32_t indexCount;
      uint32_t instanceCount;
      uint32_t firstIndex;    // prefix sum
      int32_t  vertexOffset;  // can be abused to offset into var_buffer
      uint32_t firstInstance; // caps: should be zero

      uint32_t self;
      uint32_t vboOffset;
      uint32_t padd1;
      };

  public:
    enum {
      PipelineMemorySize = 16*1024*1024,
      MeshletsMemorySize = 16*1024,
      IndirectMemorySize = 1024*sizeof(VkDrawIndexedIndirectCommand),
      };
    explicit VMeshletHelper(VDevice& dev);
    ~VMeshletHelper();

    void bindCS(VkCommandBuffer impl, VkPipelineLayout lay);
    void bindVS(VkCommandBuffer impl, VkPipelineLayout lay);

    void initRP(VkCommandBuffer impl);
    void sortPass(VkCommandBuffer impl, uint32_t meshCallsCount);
    void drawIndirect(VkCommandBuffer impl, uint32_t id);

    VkDescriptorSetLayout lay() const { return engLay; }

  private:
    void                  cleanup();
    VkDescriptorSetLayout initLayout(VDevice& device);
    VkDescriptorSetLayout initDrawLayout(VDevice& device);
    VkDescriptorPool      initPool(VDevice& device, uint32_t cnt);
    VkDescriptorSet       initDescriptors(VDevice& device, VkDescriptorPool pool, VkDescriptorSetLayout lay);
    void                  initShaders(VDevice& device);

    void                  initEngSet (VkDescriptorSet set, uint32_t cnt);
    void                  initDrawSet(VkDescriptorSet set);

    void                  barrier(VkCommandBuffer impl,
                                  VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask,
                                  VkAccessFlags srcAccessMask, VkAccessFlags dstAccessMask);

    VDevice&              dev;
    VBuffer               indirect, meshlets, scratch, compacted;

    VkDescriptorSetLayout engLay   = VK_NULL_HANDLE;
    VkDescriptorPool      engPool  = VK_NULL_HANDLE;
    VkDescriptorSet       engSet   = VK_NULL_HANDLE;

    VkDescriptorSet       compPool = VK_NULL_HANDLE;
    VkDescriptorSet       compSet  = VK_NULL_HANDLE;

    VkDescriptorSetLayout drawLay  = VK_NULL_HANDLE;
    VkDescriptorSet       drawPool = VK_NULL_HANDLE;
    VkDescriptorSet       drawSet  = VK_NULL_HANDLE;

    DSharedPtr<VPipelineLay*>  prefixSumLay;
    DSharedPtr<VCompPipeline*> prefixSum;

    DSharedPtr<VPipelineLay*>  compactageLay;
    DSharedPtr<VCompPipeline*> compactage;
  };

}
}