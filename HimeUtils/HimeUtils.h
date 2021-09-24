#pragma once

#include "Falcor.h"

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

        void createComputePass(ComputePass::SharedPtr& pComputePass, Program::DefineList defines, int groupSize, int chunkSize) const;
        void createComputePassIfNecessary(ComputePass::SharedPtr& pComputePass, int groupSize, int chunkSize, bool forceCreate) const;
    };

    class HIME_UTILS_DECL HimeBufferHelpers
    {
    public:
        static void createAndCopyBuffer(Buffer::SharedPtr& pBuffer, uint elementSize, uint elementCount, const void* pCpuData, const std::string& bufferName);
        static void createOrExtendBuffer(Buffer::SharedPtr& pBuffer, uint structSize, uint elementCount, const std::string& name);
    };

}
