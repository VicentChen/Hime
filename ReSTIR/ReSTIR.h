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
#pragma once
#include "../HimeTracer/HimePathTracer/HimePathTracer.h"

using namespace Falcor;

class ReSTIR : public HimePathTracer
{
public:
    using SharedPtr = std::shared_ptr<ReSTIR>;

    static SharedPtr create(RenderContext* pRenderContext = nullptr, const Dictionary& dict = {});

    virtual std::string getDesc() override;
    virtual RenderPassReflection reflect(const CompileData& compileData) override;
    virtual void setScene(RenderContext* pRenderContext, const std::shared_ptr<Scene>& pScene) override;
    virtual void renderUI(Gui::Widgets& widget) override;

protected:
    bool updateLights(RenderContext* pRenderContext) override;
    virtual void updateEmissiveTriangleTexture(RenderContext* pRenderContext, const RenderData& renderData) override;

private:
    ReSTIR(const Dictionary& dict);

    void bindGBuffers(ComputePass::SharedPtr& pPass, const RenderData& renderData);
    void computeNormalAndLinear(RenderContext* pRenderContext, const RenderData& renderData);

    void prepareResource(RenderContext* pRenderContext, const RenderData& renderData);
    void generateInitialSample(RenderContext* pRenderContext, const RenderData& renderData);
    void temporalResample(RenderContext* pRenderContext, const RenderData& renderData);
    void spatialResample(RenderContext* pRenderContext, const RenderData& renderData);
    void generateLightTexture(RenderContext* pRenderContext, const RenderData& renderData);
    void prepareNextFrame(RenderContext* pRenderContext, const RenderData& renderData);

    struct
    {
        bool ignoreInitialVisibility = false;
        uint initialCandidateCount = 32;

        bool enableTemporalResampling = true;
        bool enableBoilingFilter = true;
        bool regenerateTemporalResamplingShader = false;
        uint maxHistoryLength = 5; // Reduce this parameter to alleviates ghost artifact.
        uint temporalNeighborCandidateCount = 4;
        float boilingFilterStrength = 0.2f;
        float temporalNormalThreshold = 0.5f;
        float temporalDepthThreshold = 0.1f;

        bool enableSpatialResampling = true;
        uint spatialSampleCount = 1;
        float spatialSampleRadius = 32;
        uint disocclusionBoostSampleCount = 8;
        float spatialNormalThreshold = 0.5f;
        float spatialDepthThreshold = 0.1f;

        const uint neighborOffsetCount = 8192;
    } mParams;

    Buffer::SharedPtr mpCurrReservoirBuffer;
    Buffer::SharedPtr mpPrevReservoirBuffer;

    Buffer::SharedPtr mpNeighborOffsetBuffer;

    EmissivePowerSampler::SharedPtr mpEmissivePowerSampler;
    ComputePass::SharedPtr mpComputeNormalAndLinearZPass;
    ComputePass::SharedPtr mpGenerateInitialSamplePass;
    ComputePass::SharedPtr mpTemporalResamplePass;
    ComputePass::SharedPtr mpSpatialResamplePass;
    ComputePass::SharedPtr mpGenerateLightTexturePass;
};
