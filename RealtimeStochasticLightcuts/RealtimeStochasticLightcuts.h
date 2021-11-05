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
#include "../HimeUtils/BitonicSort/BitonicSort.h"
#include "../HimeTracer/HimePathTracer/HimePathTracer.h"
#include "LightTreeData.slangh"
#include "../HimeUtils/Shape/VisualizeShape.h"

using namespace Falcor;

/** Implementation of I3D 2020 paper: Realtime Stochastic Lightcuts.

    This paper focuses on direct lighting under many-light scenes.
*/
class RealtimeStochasticLightcuts : public HimePathTracer
{
public:
    using SharedPtr = std::shared_ptr<RealtimeStochasticLightcuts>;

    static SharedPtr create(RenderContext* pRenderContext = nullptr, const Dictionary& dict = {});

    virtual std::string getDesc() override;
    void renderUI(Gui::Widgets& widget) override;

protected:
    virtual void updateEmissiveTriangleTexture(RenderContext* pRenderContext, const RenderData& renderData) override;
    virtual void updateDebugTexture(RenderContext* pRenderContext, const RenderData& renderData) override;

private:
    RealtimeStochasticLightcuts(const Dictionary& dict);

    void generateLightTreeLeaves(RenderContext* pRenderContext);
    void sortTreeLeaves(RenderContext* pRenderContext);
    void constructLightTree(RenderContext* pRenderContext);
    void findLightcuts(RenderContext* pRenderContext, const RenderData& renderData);

    /** This function returns a cubic bounding box.
    */
    AABB sceneBoundHelper() const;

    struct
    {
        // Light tree params.
        bool useCPUSorter = false;
        float errorLimit = 0.001f; // [Hime]TODO: make as gui

        // Light tree infos.
        uint lightCount = 0;
        uint leafCount = 0;
        uint bogusLightCount = 0;
        uint levelCount = 0;
        uint nodeCount = 0;

        // Light tree buffers.
        std::vector<LightTreeNode> CPUBuffer;    ///< CPU buffer stores light tree.
        Buffer::SharedPtr GPUBuffer;             ///< GPU buffer stores light tree.
        Buffer::SharedPtr SortingHelperBuffer;   ///< GPU buffer stores unsorted leaves.
        Buffer::SharedPtr SortingKeyIndexBuffer; ///< GPU buffer stores key(value) and index(leaf index in buffer).
    } mLightTree;

    ComputePass::SharedPtr mpGenerateLightTreeLeavesPass;
    HimeBitonicSort::SharedPtr mpLightTreeLeavesSorter;
    ComputePass::SharedPtr mpReorderLightTreeLeavesPass;
    ComputePass::SharedPtr mpConstructLightTreePass;
    ComputePass::SharedPtr mpFindLightcutsPass;

    struct
    {
        uint2 visualizeLevelRange;
    } mDebugParams;

    ShapeVisualizer::SharedPtr mpShapeVisualizer;
};
