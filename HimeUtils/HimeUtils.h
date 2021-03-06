#pragma once

#include "Falcor.h"
#include "RenderGraph/RenderPassHelpers.h"

#ifdef BUILD_HIME_UTILS
#define HIME_UTILS_DECL __declspec(dllexport)
#else
#define HIME_UTILS_DECL __declspec(dllimport)
#endif

namespace Falcor
{
    struct HIME_UTILS_DECL HimeComputePassDesc
    {
        const std::string mFile;
        const std::string mEntryPoint;

        void createComputePass(ComputePass::SharedPtr& pComputePass, Program::DefineList defines, int groupSize, int chunkSize, const std::string& shaderModel = "") const;
        void createComputePassIfNecessary(ComputePass::SharedPtr& pComputePass, int groupSize, int chunkSize, bool forceCreate = false) const;
    };

    namespace HimeBufferHelpers
    {
        void HIME_UTILS_DECL createAndCopyBuffer(Buffer::SharedPtr& pBuffer, uint elementSize, uint elementCount, const void* pCpuData, const std::string& bufferName);
        void HIME_UTILS_DECL createOrExtendBuffer(Buffer::SharedPtr& pBuffer, uint elementSize, uint elementCount, const std::string& name);
        void HIME_UTILS_DECL createOrResizeBuffer(Buffer::SharedPtr& pBuffer, uint elementSize, uint elementCount, const std::string& name);
        void HIME_UTILS_DECL copyBufferBackToCPU(Buffer::SharedPtr& pBuffer, uint elementSize, uint elementCount, void* pCpuData);

        // [Hime]TODO: templates
    }

    namespace MortonCodeHelpers
    {
        void HIME_UTILS_DECL updateShaderVar(ShaderVar var, uint kQuantLevels, const AABB& sceneBound);
    }

    namespace HimeRenderPassHelpers
    {
        void HIME_UTILS_DECL bindChannel(ShaderVar var, const ChannelList& channelList, uint8_t channelIndex, const RenderData& renderData, const std::string& texname = "");
    }
}
