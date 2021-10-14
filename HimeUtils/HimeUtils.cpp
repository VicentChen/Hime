#include "HimeUtils.h"

using namespace Falcor;

void HimeComputePassDesc::createComputePass(ComputePass::SharedPtr& pComputePass, Program::DefineList defines, int groupSize, int chunkSize) const
{
    if (groupSize != 0) defines.add("GROUP_SIZE", std::to_string(groupSize));
    if (chunkSize != 0) defines.add("CHUNK_SIZE", std::to_string(chunkSize));
    pComputePass = ComputePass::create(mFile, mEntryPoint, defines);
}

void HimeComputePassDesc::createComputePassIfNecessary(ComputePass::SharedPtr& pComputePass, int groupSize, int chunkSize, bool forceCreate) const
{
    if (pComputePass == nullptr || forceCreate)
    {
        Shader::DefineList defines;
        if (groupSize != 0) defines.add("GROUP_SIZE", std::to_string(groupSize));
        if (chunkSize != 0) defines.add("CHUNK_SIZE", std::to_string(chunkSize));
        createComputePass(pComputePass, defines, groupSize, chunkSize);
    }
}

void HimeBufferHelpers::createAndCopyBuffer(Buffer::SharedPtr& pBuffer, uint elementSize, uint elementCount, const void* pCpuData, const std::string& bufferName)
{
    assert(pCpuData);

    // create if current buffer size < requested size
    if (!pBuffer || pBuffer->getElementCount() < elementCount)
    {
        pBuffer = Buffer::createStructured(elementSize, elementCount, Resource::BindFlags::ShaderResource | Resource::BindFlags::UnorderedAccess, Buffer::CpuAccess::None, nullptr, false);
        pBuffer->setName(bufferName);
    }

    // copy grid data to gpu
    size_t bufferSize = elementCount * elementSize;
    pBuffer->setBlob(pCpuData, 0, bufferSize);
}

void HimeBufferHelpers::createOrExtendBuffer(Buffer::SharedPtr& pBuffer, uint elementSize, uint elementCount, const std::string& name)
{
    if (pBuffer == nullptr || pBuffer->getElementCount() < elementCount)
    {
        pBuffer = Buffer::createStructured(elementSize, elementCount, ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess, Buffer::CpuAccess::None, nullptr, false);
        pBuffer->setName(name);
    }
}

void HimeBufferHelpers::copyBufferBackToCPU(Buffer::SharedPtr& pBuffer, uint elementSize, uint elementCount, void* pCpuData)
{
    void const* pGpuData = pBuffer->map(Buffer::MapType::Read);
    memcpy(pCpuData, pGpuData, elementSize * elementCount);
    pBuffer->unmap();
}

void MortonCodeHelpers::updateShaderVar(ShaderVar var, uint kQuantLevels, const AABB& sceneBound)
{
    var["PerFrameMortonCodeCB"]["quantLevels"] = kQuantLevels;
    var["PerFrameMortonCodeCB"]["sceneBound"]["minPoint"] = sceneBound.minPoint;
    var["PerFrameMortonCodeCB"]["sceneBound"]["maxPoint"] = sceneBound.maxPoint;
}
