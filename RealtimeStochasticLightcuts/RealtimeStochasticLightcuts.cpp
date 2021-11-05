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
#include "RealtimeStochasticLightcuts.h"
#include "../HimeUtils/HimeMath.h"
#include "../HimeUtils/HimeUtils.h"

namespace
{
    const char kDesc[] = "Implementation of I3D2020 paper: Realtime Stochastic Lightcuts";

    // Compute passes.
    const HimeComputePassDesc kGenerateLightTreeLeavesPass = { "RenderPasses/Hime/RealtimeStochasticLightcuts/GenerateLightTreeLeaves.cs.slang", "generateLightTreeLeaves" };
    const HimeComputePassDesc kReorderLightTreeLeavesPass  = { "RenderPasses/Hime/RealtimeStochasticLightcuts/ReorderLightTreeLeaves.cs.slang" , "reorderLightTreeLeaves"  };
    const HimeComputePassDesc kConstructLightTreePass      = { "RenderPasses/Hime/RealtimeStochasticLightcuts/ConstructLightTree.cs.slang"     , "constructLightTree"      };
    const HimeComputePassDesc kFindLightcutsPass           = { "RenderPasses/Hime/RealtimeStochasticLightcuts/FindLightCuts.cs.slang"          , "findLightcuts"           };

    // Compute shader settings.
    const uint kQuantLevels = 1024; // [Hime]TODO: make as variable
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
    lib.registerClass("RealtimeStochasticLightcuts", kDesc, RealtimeStochasticLightcuts::create);
}

RealtimeStochasticLightcuts::SharedPtr RealtimeStochasticLightcuts::create(RenderContext* pRenderContext, const Dictionary& dict)
{
    SharedPtr pPass = SharedPtr(new RealtimeStochasticLightcuts(dict));
    return pPass;
}

std::string RealtimeStochasticLightcuts::getDesc() { return kDesc; }

void RealtimeStochasticLightcuts::renderUI(Gui::Widgets& widget)
{
    auto group = widget.group("Realtime Stochastic Lightcuts", true);

    {
        auto constructLightTreeUI = group.group("Construct light tree", true);
        constructLightTreeUI.checkbox("Use CPU sorter", mLightTree.useCPUSorter, false);
    }

    {
        auto lightcutsUI = group.group("Find lightcuts", true);
        if (lightcutsUI.var("Cut size", mTracerParams.lightsPerPixel, 1u, mTracerParams.kMaxLightsPerPixel, 1u))
        {
            // It's better to bind lightSamplesPerVertex to lightsPerPixel.
            mSharedParams.lightSamplesPerVertex = mTracerParams.lightsPerPixel;
            recreateVars();
            mTracerParams.isLightsPerPixelChanged = true;
        }
    }

    {
        auto rtUI = group.group("Ray tracing", true);
        rtUI.var("Shadowrays per pixel", mTracerParams.lightsPerPixel, 1u, mTracerParams.kMaxLightsPerPixel, 1u);
    }

    {
        auto debugUI = group.group("Debug", true);
        debugUI.checkbox("Visualize light tree", mTracerParams.enableDebugTexture, false);
        if (mTracerParams.enableDebugTexture)
        {
            if (auto debugUI = widget.group("Visualize light tree", true))
            {
                debugUI.text("Note: 0 is the root node level of light tree.");
                debugUI.text("Range: 0 - " + std::to_string(mLightTree.levelCount));
                debugUI.var("Visualize level range min", mDebugParams.visualizeLevelRange.x, 0u, mLightTree.levelCount - 1, 1u);
                debugUI.var("Visualize level range max", mDebugParams.visualizeLevelRange.y, 0u, mLightTree.levelCount - 1, 1u);
                if (mDebugParams.visualizeLevelRange.x > mDebugParams.visualizeLevelRange.y)
                {
                    mDebugParams.visualizeLevelRange.x = mDebugParams.visualizeLevelRange.y;
                }
            }
        }
    }

    HimePathTracer::renderUI(widget);
}

void RealtimeStochasticLightcuts::updateEmissiveTriangleTexture(RenderContext* pRenderContext, const RenderData& renderData)
{
    PROFILE("Realtime Stochastic Lightcuts");
    generateLightTreeLeaves(pRenderContext);
    sortTreeLeaves(pRenderContext);
    constructLightTree(pRenderContext);
    findLightcuts(pRenderContext, renderData);
}

void RealtimeStochasticLightcuts::updateDebugTexture(RenderContext* renderContext, const RenderData& renderData)
{
    // Copy buffer to cpu
    Buffer::SharedPtr pLightTreeBuffer = mLightTree.GPUBuffer;
    std::vector<LightTreeNode> lightTree(pLightTreeBuffer->getElementCount());
    auto bufferSize = pLightTreeBuffer->getElementCount() * sizeof(LightTreeNode);
    LightTreeNode* pLightTree = (LightTreeNode*)pLightTreeBuffer->map(Buffer::MapType::Read);
    memcpy(lightTree.data(), pLightTree, bufferSize);
    pLightTreeBuffer->unmap();

    // compute level index
    auto nodeCount = lightTree.size();
    auto levelCount = uintLog2((uint)nodeCount) + 1;

    std::vector<std::pair<uint, uint>> levelIndex;
    {
        uint beginIndex = 0; uint endIndex = 0;
        for (uint i = 0; i <= levelCount; i++)
        {
            endIndex = (uint)pow(2, i);
            levelIndex.emplace_back(std::make_pair(beginIndex, endIndex));
            beginIndex = endIndex;
        }
    }

    // compute transform matrix
    WiredCubes wiredCubes;
    for (auto i = mDebugParams.visualizeLevelRange.x; i <= mDebugParams.visualizeLevelRange.y; i++)
    {
        for (auto k = levelIndex[i].first; k < levelIndex[i].second; k++)
        {
            if (lightTree[k].isBogus()) break;
            wiredCubes.addInstance(lightTree[k].aabbMinPoint, lightTree[k].aabbMaxPoint);
        }
    }

    Texture::SharedPtr pDebugTexture = getDebugTexture(renderData);
    Texture::SharedPtr pPositionTexture = getPositionTexture(renderData);
    mpShapeVisualizer->drawWiredCubes(renderContext, wiredCubes, float3(0, 1, 0), pDebugTexture, mpScene->getCamera(), pPositionTexture);
}

RealtimeStochasticLightcuts::RealtimeStochasticLightcuts(const Dictionary& dict)
    : HimePathTracer(dict)
{
    // Set to true here. Otherwise this will lead to darker result.
    mTracerParams.accumulateShadowRay = true;
    mDebugParams.visualizeLevelRange = uint2(0, 0);

    mpLightTreeLeavesSorter = HimeBitonicSort::create(true); // we are using key index, which is uint2 = 64bit
    mpShapeVisualizer = ShapeVisualizer::create();
}

void RealtimeStochasticLightcuts::generateLightTreeLeaves(RenderContext* pRenderContext)
{
    PROFILE("Generate Light Tree Leaves");
    assert(mpScene);

    // Create leaf nodes generating program
    if (mpGenerateLightTreeLeavesPass == nullptr)
    {
        Program::DefineList defines = mpScene->getSceneDefines();
        kGenerateLightTreeLeavesPass.createComputePass(mpGenerateLightTreeLeavesPass, defines, kGroupSize, kChunkSize);
    }

    mLightTree.lightCount = mpScene->getLightCollection(pRenderContext)->getTotalLightCount();
    mLightTree.leafCount = nextPow2(mLightTree.lightCount);
    mLightTree.bogusLightCount = mLightTree.leafCount - mLightTree.lightCount;
    mLightTree.levelCount = uintLog2(mLightTree.leafCount) + 1;
    mLightTree.nodeCount = CompleteBinaryTreeHelpers::getAllNodeCount(mLightTree.levelCount - 1);

    HimeBufferHelpers::createOrExtendBuffer(mLightTree.GPUBuffer, sizeof(LightTreeNode), mLightTree.nodeCount, "Lightcuts::LightTreeBuffer");
    HimeBufferHelpers::createOrExtendBuffer(mLightTree.SortingHelperBuffer, sizeof(LightTreeNode), mLightTree.lightCount, "Lightcuts::SortingHelperBuffer");
    HimeBufferHelpers::createOrExtendBuffer(mLightTree.SortingKeyIndexBuffer, sizeof(uint2), mLightTree.lightCount, "Lightcuts::SortingKeyIndexBuffer");

    MortonCodeHelpers::updateShaderVar(mpGenerateLightTreeLeavesPass.getRootVar(), kQuantLevels, sceneBoundHelper());
    mpGenerateLightTreeLeavesPass.getRootVar()["gScene"] = mpScene->getParameterBlock();
    mpGenerateLightTreeLeavesPass.getRootVar()["PerFrameCB"]["lightCount"] = mLightTree.lightCount;
    mpGenerateLightTreeLeavesPass.getRootVar()["PerFrameCB"]["levelCount"] = mLightTree.levelCount;
    mpGenerateLightTreeLeavesPass.getRootVar()["gLightTree"] = mLightTree.GPUBuffer;
    mpGenerateLightTreeLeavesPass.getRootVar()["gSortingHelper"] = mLightTree.SortingHelperBuffer;
    mpGenerateLightTreeLeavesPass.getRootVar()["gSortingKeyIndex"] = mLightTree.SortingKeyIndexBuffer;

    mpGenerateLightTreeLeavesPass->execute(pRenderContext, mLightTree.leafCount, 1, 1);
}

void RealtimeStochasticLightcuts::sortTreeLeaves(RenderContext* pRenderContext)
{
    PROFILE("Sort Light Tree Leaves");
    auto& lightTreeCPUBuffer = mLightTree.CPUBuffer;
    auto& lightTreeGPUBuffer = mLightTree.GPUBuffer;

    if (mLightTree.useCPUSorter)
    {
        PROFILE("CPU Sort Light Tree Leaves");
        // Copy buffer to cpu, sort and copy back to gpu
        uint32_t leafStartIdx = CompleteBinaryTreeHelpers::getCurrentLevelNodeStartIdx(mLightTree.levelCount - 1);
        size_t bufferSize = sizeof(LightTreeNode) * mLightTree.lightCount;

        lightTreeCPUBuffer.resize(mLightTree.lightCount);
        LightTreeNode* pTree = (LightTreeNode*)lightTreeGPUBuffer->map(Buffer::MapType::Read);
        memcpy(lightTreeCPUBuffer.data(), &(pTree[leafStartIdx]), bufferSize);
        lightTreeGPUBuffer->unmap();

        std::sort(lightTreeCPUBuffer.begin(), lightTreeCPUBuffer.end(), [](const LightTreeNode& a, const LightTreeNode& b) { return a.mortonCode < b.mortonCode; });

        uint32_t leafOffsetInByte = leafStartIdx * sizeof(LightTreeNode);
        lightTreeGPUBuffer->setBlob(lightTreeCPUBuffer.data(), leafOffsetInByte, bufferSize);
    }
    else
    {
        {
            PROFILE("GPU Sort Light Tree Leaves");

            mpLightTreeLeavesSorter->sort(pRenderContext, mLightTree.SortingKeyIndexBuffer, mLightTree.lightCount);
        }
        PROFILE("Reorder Light Tree Leaves");
        {
            kReorderLightTreeLeavesPass.createComputePassIfNecessary(mpReorderLightTreeLeavesPass, kGroupSize, kChunkSize, false);

            mpReorderLightTreeLeavesPass.getRootVar()["PerFrameCB"]["lightCount"] = mLightTree.lightCount;
            mpReorderLightTreeLeavesPass.getRootVar()["PerFrameCB"]["levelCount"] = mLightTree.levelCount;
            mpReorderLightTreeLeavesPass.getRootVar()["gLightTree"] = mLightTree.GPUBuffer;
            mpReorderLightTreeLeavesPass.getRootVar()["gSortingHelper"] = mLightTree.SortingHelperBuffer;
            mpReorderLightTreeLeavesPass.getRootVar()["gSortingKeyIndex"] = mLightTree.SortingKeyIndexBuffer;

            mpReorderLightTreeLeavesPass->execute(pRenderContext, uint3(mLightTree.lightCount, 1, 1));
        }
    }
}

void RealtimeStochasticLightcuts::constructLightTree(RenderContext* pRenderContext)
{
    PROFILE("Construct Light Tree");

    // create light tree construction program
    const int kMaxWorkLoad = 2048;

    kConstructLightTreePass.createComputePassIfNecessary(mpConstructLightTreePass, kGroupSize, kChunkSize, false);

    for (int srcLevel = mLightTree.levelCount - 1; srcLevel > 0;)
    {
        int workLoad = 0;

        int dstLevelStart = srcLevel - 1;
        while(dstLevelStart >= 0)
        {
            workLoad += CompleteBinaryTreeHelpers::getCurrentLevelNodeCount(dstLevelStart);
            dstLevelStart--;
            if (workLoad > kMaxWorkLoad) break;
        }
        dstLevelStart++;
        int dstLevelEnd = srcLevel - 1;

        {
            PROFILE("Construct Light Tree Level " + std::to_string(dstLevelStart) + "-" + std::to_string(dstLevelEnd));
            MortonCodeHelpers::updateShaderVar(mpConstructLightTreePass.getRootVar(), kQuantLevels, sceneBoundHelper());
            mpConstructLightTreePass.getRootVar()["PerFrameCB"]["workLoad"] = workLoad;
            mpConstructLightTreePass.getRootVar()["PerFrameCB"]["srcLevel"] = srcLevel;
            mpConstructLightTreePass.getRootVar()["PerFrameCB"]["dstLevelStart"] = dstLevelStart;
            mpConstructLightTreePass.getRootVar()["PerFrameCB"]["dstLevelEnd"] = dstLevelEnd;
            mpConstructLightTreePass.getRootVar()["gLightTree"] = mLightTree.GPUBuffer;
            mpConstructLightTreePass->execute(pRenderContext, workLoad, 1, 1);
        }

        srcLevel = dstLevelStart;
    }
}

void RealtimeStochasticLightcuts::findLightcuts(RenderContext* pRenderContext, const RenderData& renderData)
{
    PROFILE("Find Lightcuts");

    if (mpFindLightcutsPass == nullptr || mTracerParams.isLightsPerPixelChanged)
    {
        // [Hime]TODO: may be we will modify "MAX_LIGHT_SAMPLES" and regenerate this program
        assert(mpScene);
        Program::DefineList defines = mpScene->getSceneDefines();
        defines.add(getValidResourceDefines(mInputChannels, renderData)); // We need `loadShadingData`, which uses input channels
        defines.add(mpSampleGenerator->getDefines()); // We need `SampleGenerator`
        defines.add("NUM_LIGHT_SAMPLES", std::to_string(mTracerParams.lightsPerPixel));
        defines.add("USE_VBUFFER", mSharedParams.useVBuffer ? "1" : "0");
        defines.add("GBUFFER_ADJUST_SHADING_NORMALS", mGBufferAdjustShadingNormals ? "1" : "0");
        kFindLightcutsPass.createComputePass(mpFindLightcutsPass, defines, kGroupSize, kChunkSize);

        mTracerParams.isLightsPerPixelChanged = false;
    }

    Texture::SharedPtr pLightIndexTexture = getEmissiveTriangleTexture(renderData);
    auto sceneBound = sceneBoundHelper();

    // Add missed channels here
    // Bind I/O buffers. These needs to be done per-frame as the buffers may change anytime.
    auto bind = [&](const ChannelDesc& desc)
    {
        if (!desc.texname.empty())
        {
            auto pGlobalVars = mpFindLightcutsPass.getRootVar();
            pGlobalVars[desc.texname] = renderData[desc.name]->asTexture();
        }
    };
    for (auto channel : mInputChannels) bind(channel);

    mpFindLightcutsPass.getRootVar()["gScene"] = mpScene->getParameterBlock(); // [Hime]TODO: Do we need this?
    mpFindLightcutsPass.getRootVar()["PerFrameCB"]["dispatchDim"] = mSharedParams.frameDim;
    mpFindLightcutsPass.getRootVar()["PerFrameCB"]["frameCount"] = mSharedParams.frameCount;
    mpFindLightcutsPass.getRootVar()["PerFrameCB"]["levelCount"] = mLightTree.levelCount;
    mpFindLightcutsPass.getRootVar()["PerFrameCB"]["errorLimit"] = mLightTree.errorLimit;
    mpFindLightcutsPass.getRootVar()["PerFrameCB"]["sceneLightBoundRadius"] = sceneBound.radius();
    mpFindLightcutsPass.getRootVar()["gLightTree"] = mLightTree.GPUBuffer;
    mpFindLightcutsPass.getRootVar()["gLightIndex"] = pLightIndexTexture;

    mpFindLightcutsPass->execute(pRenderContext, uint3(mSharedParams.frameDim, 1));
}

AABB RealtimeStochasticLightcuts::sceneBoundHelper() const
{
    const auto& sceneBound = mpScene->getSceneBounds();
    AABB hackedSceneBound = sceneBound;
    auto extent = hackedSceneBound.extent();
    float maxExtent = std::max(extent.x, std::max(extent.y, extent.z));
    hackedSceneBound.maxPoint = hackedSceneBound.minPoint + float3(maxExtent, maxExtent, maxExtent);
    return hackedSceneBound;
}
