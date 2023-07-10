#pragma once
#include <donut/render/GBuffer.h>
#include <nvrhi/nvrhi.h>
#include <memory>
#include <donut/render/ForwardShadingPass.h>
#include <donut/engine/FramebufferFactory.h>
#include <nvrhi/common/misc.h>

using namespace donut::render;
using namespace donut::engine;
using namespace donut::math;

class RenderTargets : public GBufferRenderTargets
{
public:
    nvrhi::TextureHandle HdrColor;
    nvrhi::TextureHandle LdrColor;
    nvrhi::TextureHandle MaterialIDs;
    nvrhi::TextureHandle ResolvedColor;
    nvrhi::TextureHandle TemporalFeedback1;
    nvrhi::TextureHandle TemporalFeedback2;
    nvrhi::TextureHandle AmbientOcclusion;

    nvrhi::HeapHandle Heap;

    std::shared_ptr<FramebufferFactory> ForwardFramebuffer;
    std::shared_ptr<FramebufferFactory> HdrFramebuffer;
    std::shared_ptr<FramebufferFactory> LdrFramebuffer;
    std::shared_ptr<FramebufferFactory> ResolvedFramebuffer;
    std::shared_ptr<FramebufferFactory> MaterialIDFramebuffer;

    void Init(
        nvrhi::IDevice* device,
        dm::uint2 size,
        dm::uint sampleCount,
        bool enableMotionVectors,
        bool useReverseProjection) override;

    [[nodiscard]] bool IsUpdateRequired(dm::uint2 size, dm::uint sampleCount) const;

    void Clear(nvrhi::ICommandList* commandList) override;
};