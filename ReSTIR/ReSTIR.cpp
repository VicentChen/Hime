/***************************************************************************
 # Copyright (c) 2015-21, NVIDIA CORPORATION. All rights reserved.
 #
 # Redistribution and use in source and binary forms, with or without
 # modification, are permitted provided that the following conditions
 # are met:
 #  * Redistributions of source code must retain the above copyright
 #    notice, this list of conditions and the following disclaimer.
 #  * Redistributions in binary form must reproduce the above copyright
 #    notice, this list of conditions and the following disclaimer in the
 #    documentation and/or other materials provided with the distribution.
 #  * Neither the name of NVIDIA CORPORATION nor the names of its
 #    contributors may be used to endorse or promote products derived
 #    from this software without specific prior written permission.
 #
 # THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS "AS IS" AND ANY
 # EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 # IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 # PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 # CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 # EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 # PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 # PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 # OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 # (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 # OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 **************************************************************************/
#include "ReSTIR.h"
#include "../HimeUtils/HimeUtils.h"
#include "ReservoirData.slang"

namespace
{
    const char kDesc[] = "Implementation of SIGGRAPH2020 paper: "
        "Spatiotemporal Reservoir Resampling for Real-Time Ray Tracing with Dynamic Direct Lighting";

    // Render pass extra input channels.
    enum ExtraInputChannelEnum : uint8_t
    {
        MotionVector = 0,
        LinearZAndDeriv,
    };
    const Falcor::ChannelList kExtraInputChannels =
    {
        /* MotionVector    */ { "MotionVector"   , "gMotionVector"   , "Motion vector"         , false, ResourceFormat::RG32Float },
        /* LinearZAndDeriv */ { "LinearZAndDeriv", "gLinearZAndDeriv", "Linear z and derivates", false, ResourceFormat::RG32Float },
    };

    // Render pass internal channels.
    enum InternalChannelEnum : uint8_t
    {
        PrevNormalAndLinearZ = 0,
    };
    const Falcor::ChannelList kInternalChannels =
    {
        /* PrevNomralAndLinearZ */ { "PrevNormalAndLinearZ", "gPrevNormalAndLinearZ", "Packed normal and linear z in previous frame", false, ResourceFormat::RGBA32Float },
    };

    // Compute passes.
    const HimeComputePassDesc kComputeNormalAndLinearZPass = { "RenderPasses/Hime/ReSTIR/ComputeNormalAndLinearZ.cs.slang" , "computeNormalAndLinearZ" };
    const HimeComputePassDesc kGenerateInitialSamplePass   = { "RenderPasses/Hime/ReSTIR/GenerateInitialSample.cs.slang"   , "generateInitialSample"   };
    const HimeComputePassDesc kTemporalResamplePass        = { "RenderPasses/Hime/ReSTIR/TemporalResample.cs.slang"        , "temporalResample"        };
    const HimeComputePassDesc kSpatialResamplePass         = { "RenderPasses/Hime/ReSTIR/SpatialResample.cs.slang"         , "spatialResample"         };
    const HimeComputePassDesc kGenerateLightTexturePass    = { "RenderPasses/Hime/ReSTIR/GenerateLightTexture.cs.slang"    , "generateLightTexture"    };

    // Compute shader settings.
    const uint kGroupSize = 512;
    const uint kChunkSize = 16;
}

// Don't remove this. it's required for hot-reload to function properly
extern "C" __declspec(dllexport) const char* getProjDir()
{
    return PROJECT_DIR;
}

extern "C" __declspec(dllexport) void getPasses(Falcor::RenderPassLibrary& lib)
{
    lib.registerClass("ReSTIR", kDesc, ReSTIR::create);
}

ReSTIR::SharedPtr ReSTIR::create(RenderContext* pRenderContext, const Dictionary& dict)
{
    return SharedPtr(new ReSTIR(dict));
}

std::string ReSTIR::getDesc() { return kDesc; }

RenderPassReflection ReSTIR::reflect(const CompileData& compile_data)
{
    auto reflector = HimePathTracer::reflect(compile_data);

    addRenderPassInputs(reflector, kExtraInputChannels);

    for (const auto& internalChannel : kInternalChannels)
    {
        reflector.addInternal(internalChannel.name, internalChannel.desc)
            .format(internalChannel.format)
            .bindFlags(ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess);
    }

    return reflector;
}

void ReSTIR::setScene(RenderContext* pRenderContext, const std::shared_ptr<Scene>& pScene)
{
    HimePathTracer::setScene(pRenderContext, pScene);
    mpEmissivePowerSampler = nullptr;
}

void ReSTIR::renderUI(Gui::Widgets& widget)
{
    auto group = widget.group("ReSTIR", true);

    {
        auto initialCandidatePassUI = group.group("Generate initial sample", true);
        initialCandidatePassUI.checkbox("Disable initial visibility", mParams.ignoreInitialVisibility);
        initialCandidatePassUI.var("Initial candidates count", mParams.initialCandidateCount, 1u, 1024u, 1u);
    }

    {
        auto temporalPassUI = group.group("Temporal resampling", true);
        temporalPassUI.checkbox("Enable temporal resampling", mParams.enableTemporalResampling);
        if (mParams.enableTemporalResampling)
        {
            temporalPassUI.var("Max history length", mParams.maxHistoryLength, 1u, 32u, 1u);
            temporalPassUI.var("Temporal neightbor candidate count", mParams.temporalNeighborCandidateCount, 1u, 16u, 1u);

            if (temporalPassUI.checkbox("Enable boiling filter", mParams.enableBoilingFilter))
            {
                mParams.regenerateTemporalResamplingShader = true;
            }

            if (mParams.enableBoilingFilter)
            {
                temporalPassUI.var("Boiling filter strength", mParams.boilingFilterStrength, 0.0f, 1.0f, 0.01f, true);
            }
        }
    }

    {
        auto spatialPassUI = group.group("Spatial resampling", true);
        spatialPassUI.checkbox("Enable spatial resampling", mParams.enableSpatialResampling);
        if (mParams.enableSpatialResampling)
        {
            spatialPassUI.var("Spatial sample count", mParams.spatialSampleCount, 1u, 32u, 1u);
            spatialPassUI.var("Spatial sample radius", mParams.spatialSampleRadius, 1.0f, 250.0f, 1.0f);
            spatialPassUI.var("Disocclusion boost samples", mParams.disocclusionBoostSampleCount, 1u, 128u, 1u);
        }
    }

    {
        auto lightIndexPassUI = group.group("Generate light index", true);
        // Seems that ray tracing shaders will be re-compiled automatically in HimePathTracer::execute(), prepareVar().
        lightIndexPassUI.checkbox("Disable final visibility", mTracerParams.ignoreShadowRayVisibility);
    }

    HimePathTracer::renderUI(widget);
}

bool ReSTIR::updateLights(RenderContext* pRenderContext)
{
    bool lightingChanged = PathTracer::updateLights(pRenderContext);

    // Update EmissivePowerSampler here to achieve better performance.
    if (mpScene && mpEmissivePowerSampler == nullptr)
    {
        mpEmissivePowerSampler = EmissivePowerSampler::create(pRenderContext, mpScene);
    }

    if (mpEmissivePowerSampler)
    {
        lightingChanged |= mpEmissivePowerSampler->update(pRenderContext);
    }
    return lightingChanged;
}

void ReSTIR::updateEmissiveTriangleTexture(RenderContext* pRenderContext, const RenderData& renderData)
{
    PROFILE("ReSTIR");
    prepareResource(pRenderContext, renderData);
    generateInitialSample(pRenderContext, renderData);
    temporalResample(pRenderContext, renderData);
    computeNormalAndLinear(pRenderContext, renderData);
    spatialResample(pRenderContext, renderData);
    generateLightTexture(pRenderContext, renderData);
    prepareNextFrame(pRenderContext, renderData);
}

ReSTIR::ReSTIR(const Dictionary& dict)
    : HimePathTracer(dict)
{
    // ReSTIR provides light index, light index pdf and light uv.
    mTracerParams.sampleWithProvidedUV = true;
}

void ReSTIR::bindGBuffers(ComputePass::SharedPtr& pPass, const RenderData& renderData)
{
    // Add missed channels here
    // Bind I/O buffers. These needs to be done per-frame as the buffers may change anytime.
    auto bind = [&](const ChannelDesc& desc)
    {
        if (!desc.texname.empty())
        {
            auto pGlobalVars = pPass.getRootVar();
            pGlobalVars[desc.texname] = renderData[desc.name]->asTexture();
        }
    };
    for (auto channel : mInputChannels) bind(channel);
}

void ReSTIR::computeNormalAndLinear(RenderContext* pRenderContext, const RenderData& renderData)
{
    // Pack current frame's normal and linear z here.
    // Current texture will be used in next frame's temporalResample().
    PROFILE("Compute normal and linear z");

    // Prepare normal and linear z for next frame.
    if (mpComputeNormalAndLinearZPass == nullptr)
    {
        Program::DefineList defines = mpScene->getSceneDefines();
        defines.add(getValidResourceDefines(mInputChannels, renderData)); // We need `loadShadingData`, which uses input channels
        defines.add("USE_VBUFFER", mSharedParams.useVBuffer ? "1" : "0");
        defines.add("GBUFFER_ADJUST_SHADING_NORMALS", mGBufferAdjustShadingNormals ? "1" : "0");
        kComputeNormalAndLinearZPass.createComputePass(mpComputeNormalAndLinearZPass, defines, kGroupSize, kChunkSize);
    }

    bindGBuffers(mpComputeNormalAndLinearZPass, renderData);
    HimeRenderPassHelpers::bindChannel(mpComputeNormalAndLinearZPass.getRootVar(), kExtraInputChannels, LinearZAndDeriv, renderData);
    HimeRenderPassHelpers::bindChannel(mpComputeNormalAndLinearZPass.getRootVar(), kInternalChannels, PrevNormalAndLinearZ, renderData);
    mpComputeNormalAndLinearZPass.getRootVar()["PerFrameCB"]["dispatchDim"] = mSharedParams.frameDim;
    mpComputeNormalAndLinearZPass.getRootVar()["gScene"] = mpScene->getParameterBlock();

    mpComputeNormalAndLinearZPass->execute(pRenderContext, uint3(mSharedParams.frameDim, 1));
}

void ReSTIR::prepareResource(RenderContext* pRenderContext, const RenderData& renderData)
{
    // Precompute neighbor "random" offset buffer.
    {
        std::vector<float2> neighborOffsets(mParams.neighborOffsetCount);
        const float phi2 = 1.0f / 1.3247179572447f;
        uint32_t num = 0;
        float u = 0.5f;
        float v = 0.5f;
        while (num < neighborOffsets.size()) {
            u += phi2;
            v += phi2 * phi2;
            if (u >= 1.0f) u -= 1.0f;
            if (v >= 1.0f) v -= 1.0f;

            float rSq = (u - 0.5f) * (u - 0.5f) + (v - 0.5f) * (v - 0.5f);
            if (rSq > 0.25f) continue;

            neighborOffsets[num++] = float2(u - 0.5f, v - 0.5f);
        }
        HimeBufferHelpers::createAndCopyBuffer(mpNeighborOffsetBuffer, sizeof(float2), (uint)neighborOffsets.size(), neighborOffsets.data(), "ReSTIR::NeighborOffsetBuffer");
    }
}

void ReSTIR::generateInitialSample(RenderContext* pRenderContext, const RenderData& renderData)
{
    PROFILE("Generate initial sample");
    if (mpGenerateInitialSamplePass == nullptr)
    {
        Program::DefineList defines = mpScene->getSceneDefines();
        defines.add(getValidResourceDefines(mInputChannels, renderData)); // We need `loadShadingData`, which uses input channels
        defines.add(mpScene->getSceneDefines());
        defines.add(mpSampleGenerator->getDefines()); // We need `SampleGenerator`
        defines.add("USE_VBUFFER", mSharedParams.useVBuffer ? "1" : "0");
        defines.add("GBUFFER_ADJUST_SHADING_NORMALS", mGBufferAdjustShadingNormals ? "1" : "0");
        defines.add("CHUNK_SIZE", std::to_string(kChunkSize));

        kGenerateInitialSamplePass.createComputePass(mpGenerateInitialSamplePass, defines, kGroupSize, kChunkSize, "6_5");
    }

    HimeBufferHelpers::createOrResizeBuffer(mpCurrReservoirBuffer, sizeof(PackedReservoirData), mSharedParams.frameDim.x * mSharedParams.frameDim.y, "ReSTIR::CurrReservoirBuffer");
    HimeBufferHelpers::createOrResizeBuffer(mpPrevReservoirBuffer, sizeof(PackedReservoirData), mSharedParams.frameDim.x * mSharedParams.frameDim.y, "ReSTIR::PrevReservoirBuffer");

    bindGBuffers(mpGenerateInitialSamplePass, renderData);

    mpScene->setRaytracingShaderData(pRenderContext, mpGenerateInitialSamplePass.getRootVar());
    mpEmissivePowerSampler->setShaderData(mpGenerateInitialSamplePass.getRootVar()["PerFrameCB"]["emissivePowerSampler"]);
    mpGenerateInitialSamplePass.getRootVar()["PerFrameCB"]["dispatchDim"] = mSharedParams.frameDim;
    mpGenerateInitialSamplePass.getRootVar()["PerFrameCB"]["frameCount"] = mSharedParams.frameCount;
    mpGenerateInitialSamplePass.getRootVar()["PerFrameCB"]["candidateCount"] = mParams.initialCandidateCount;
    mpGenerateInitialSamplePass.getRootVar()["PerFrameCB"]["ignoreVisibility"] = mParams.ignoreInitialVisibility;
    mpGenerateInitialSamplePass.getRootVar()["gReservoirBuffer"] = mpCurrReservoirBuffer;
    mpGenerateInitialSamplePass->execute(pRenderContext, uint3(mSharedParams.frameDim, 1));
}

void ReSTIR::temporalResample(RenderContext* pRenderContext, const RenderData& renderData)
{
    if (!mParams.enableTemporalResampling) return;

    PROFILE("Temporal resample");

    if (mParams.regenerateTemporalResamplingShader || mpTemporalResamplePass == nullptr)
    {
        mParams.regenerateTemporalResamplingShader = false;

        Program::DefineList defines = mpScene->getSceneDefines();
        defines.add(getValidResourceDefines(mInputChannels, renderData)); // We need `loadShadingData`, which uses input channels
        defines.add(mpSampleGenerator->getDefines()); // We need `SampleGenerator`
        defines.add("USE_VBUFFER", mSharedParams.useVBuffer ? "1" : "0");
        defines.add("GBUFFER_ADJUST_SHADING_NORMALS", mGBufferAdjustShadingNormals ? "1" : "0");
        defines.add("ENABLE_BOILING_FILTER", mParams.enableBoilingFilter ? "1" : "0");
        kTemporalResamplePass.createComputePass(mpTemporalResamplePass, defines, kGroupSize, kChunkSize);
    }

    bindGBuffers(mpTemporalResamplePass, renderData);
    HimeRenderPassHelpers::bindChannel(mpTemporalResamplePass.getRootVar(), kExtraInputChannels, MotionVector, renderData);
    HimeRenderPassHelpers::bindChannel(mpTemporalResamplePass.getRootVar(), kInternalChannels, PrevNormalAndLinearZ, renderData);
    mpTemporalResamplePass.getRootVar()["PerFrameCB"]["dispatchDim"] = mSharedParams.frameDim;
    mpTemporalResamplePass.getRootVar()["PerFrameCB"]["frameCount"] = mSharedParams.frameCount;
    mpTemporalResamplePass.getRootVar()["PerFrameCB"]["maxHistoryLength"] = mParams.maxHistoryLength;
    mpTemporalResamplePass.getRootVar()["PerFrameCB"]["temporalNeighborCandidateCount"] = mParams.temporalNeighborCandidateCount;
    mpTemporalResamplePass.getRootVar()["PerFrameCB"]["boilingFilterStrength"] = mParams.boilingFilterStrength;
    mpTemporalResamplePass.getRootVar()["PerFrameCB"]["normalThreshold"] = mParams.temporalNormalThreshold;
    mpTemporalResamplePass.getRootVar()["PerFrameCB"]["depthThreshold"] = mParams.temporalDepthThreshold;
    mpTemporalResamplePass.getRootVar()["gScene"] = mpScene->getParameterBlock();
    mpTemporalResamplePass.getRootVar()["gPrevReservoirBuffer"] = mpPrevReservoirBuffer;
    mpTemporalResamplePass.getRootVar()["gCurrReservoirBuffer"] = mpCurrReservoirBuffer;
    mpTemporalResamplePass->execute(pRenderContext, uint3(mSharedParams.frameDim, 1));
}

void ReSTIR::spatialResample(RenderContext* pRenderContext, const RenderData& renderData)
{
    if (!mParams.enableSpatialResampling) return;

    PROFILE("Spatial resample");

    if (mpSpatialResamplePass == nullptr)
    {
        Program::DefineList defines = mpScene->getSceneDefines();
        defines.add(getValidResourceDefines(mInputChannels, renderData)); // We need `loadShadingData`, which uses input channels
        defines.add(mpSampleGenerator->getDefines()); // We need `SampleGenerator`
        defines.add("USE_VBUFFER", mSharedParams.useVBuffer ? "1" : "0");
        defines.add("GBUFFER_ADJUST_SHADING_NORMALS", mGBufferAdjustShadingNormals ? "1" : "0");
        kSpatialResamplePass.createComputePass(mpSpatialResamplePass, defines, kGroupSize, kChunkSize);
    }

    bindGBuffers(mpSpatialResamplePass, renderData);

    HimeRenderPassHelpers::bindChannel(mpSpatialResamplePass.getRootVar(), kInternalChannels, PrevNormalAndLinearZ, renderData, "gNormalAndLinearZ");
    mpSpatialResamplePass.getRootVar()["PerFrameCB"]["dispatchDim"] = mSharedParams.frameDim;
    mpSpatialResamplePass.getRootVar()["PerFrameCB"]["frameCount"] = mSharedParams.frameCount;
    mpSpatialResamplePass.getRootVar()["PerFrameCB"]["neighborOffsetCount"] = mParams.neighborOffsetCount;
    mpSpatialResamplePass.getRootVar()["PerFrameCB"]["spatialSampleCount"] = mParams.spatialSampleCount;
    mpSpatialResamplePass.getRootVar()["PerFrameCB"]["spatialSampleRadius"] = mParams.spatialSampleRadius;
    mpSpatialResamplePass.getRootVar()["PerFrameCB"]["maxHistoryLength"] = mParams.maxHistoryLength;
    mpSpatialResamplePass.getRootVar()["PerFrameCB"]["disocclusionBoostSampleCount"] = mParams.disocclusionBoostSampleCount;
    mpSpatialResamplePass.getRootVar()["PerFrameCB"]["normalThreshold"] = mParams.spatialNormalThreshold;
    mpSpatialResamplePass.getRootVar()["PerFrameCB"]["depthThreshold"] = mParams.spatialDepthThreshold;
    mpSpatialResamplePass.getRootVar()["gScene"] = mpScene->getParameterBlock();
    mpSpatialResamplePass.getRootVar()["gReservoirBuffer"] = mpCurrReservoirBuffer;
    mpSpatialResamplePass.getRootVar()["gNeighborOffsetBuffer"] = mpNeighborOffsetBuffer;
    mpSpatialResamplePass->execute(pRenderContext, uint3(mSharedParams.frameDim, 1));
}

void ReSTIR::generateLightTexture(RenderContext* pRenderContext, const RenderData& renderData)
{
    PROFILE("Generate light texture");

    kGenerateLightTexturePass.createComputePassIfNecessary(mpGenerateLightTexturePass, kGroupSize, kChunkSize);

    Texture::SharedPtr pTexture = getEmissiveTriangleTexture(renderData);

    mpGenerateLightTexturePass.getRootVar()["PerFrameCB"]["dispatchDim"] = mSharedParams.frameDim;
    mpGenerateLightTexturePass.getRootVar()["PerFrameCB"]["frameCount"] = mSharedParams.frameCount;
    mpGenerateLightTexturePass.getRootVar()["gReservoirBuffer"] = mpCurrReservoirBuffer;
    mpGenerateLightTexturePass.getRootVar()["gLightTexture"] = pTexture;
    mpGenerateLightTexturePass->execute(pRenderContext, uint3(mSharedParams.frameDim, 1));
}

void ReSTIR::prepareNextFrame(RenderContext* pRenderContext, const RenderData& renderData)
{
    // Swap current reservoir buffer and previous reservoir buffer.
    // Use current reservoir buffer in current frame as previous reservoir buffer in next frame.
    std::swap(mpCurrReservoirBuffer, mpPrevReservoirBuffer);
}
