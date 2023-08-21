#pragma once

#include <Tempest/AbstractGraphicsApi>
#include <Metal/Metal.hpp>

#include "nsptr.h"

namespace Tempest {
namespace Detail {

class MtDevice;
class MtBuffer;

struct MtBlasBuildCtx : AbstractGraphicsApi::BlasBuildCtx {
  void pushGeometry(MtDevice& dx,
                    const MtBuffer& vbo, size_t vboSz, size_t stride,
                    const MtBuffer& ibo, size_t iboSz, size_t ioffset, IndexClass icls);
  void bake();

  MTL::AccelerationStructureSizes buildSizes(MtDevice& dx) const;

  std::vector<NsPtr<MTL::AccelerationStructureTriangleGeometryDescriptor>> ranges;
  std::vector<const NS::Object*>                                           geo;
  NsPtr<MTL::PrimitiveAccelerationStructureDescriptor>                     desc;
  };

class MtAccelerationStructure : public Tempest::AbstractGraphicsApi::AccelerationStructure {
  public:
    MtAccelerationStructure(MtDevice& owner,
                            MtBuffer& vbo, size_t vboSz, size_t stride,
                            MtBuffer& ibo, size_t iboSz, size_t ioffset, Detail::IndexClass icls);
    MtAccelerationStructure(MtDevice& owner, const AbstractGraphicsApi::RtGeometry* geom, size_t size);
    ~MtAccelerationStructure();

    MtDevice&                         owner;
    NsPtr<MTL::AccelerationStructure> impl;
  };

class MtTopAccelerationStructure : public AbstractGraphicsApi::AccelerationStructure {
  public:
    MtTopAccelerationStructure(MtDevice& owner, const RtInstance* inst, AccelerationStructure* const * as, size_t asSize);
    ~MtTopAccelerationStructure();

    MtDevice&                         owner;
    NsPtr<MTL::AccelerationStructure> impl;
    NsPtr<MTL::Buffer>                instances;

    std::vector<MTL::Resource*>       blas;
  };
}
}
