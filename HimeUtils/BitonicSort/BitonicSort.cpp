#include "stdafx.h"
#include "BitonicSort.h"

namespace Falcor
{
    static const char kBitonicIndirectArgsFilename[] = "RenderPasses/Hime/HimeUtils/BitonicSort/IndirectArgs.cs.slang";
    static const char kBitonicPreSortFilename[] = "RenderPasses/Hime/HimeUtils/BitonicSort/PreSort.cs.slang";
    static const char kBitonicOuterSortFilename[] = "RenderPasses/Hime/HimeUtils/BitonicSort/OuterSort.cs.slang";
    static const char kBitonicInnerSortFilename[] = "RenderPasses/Hime/HimeUtils/BitonicSort/InnerSort.cs.slang";

    HimeBitonicSort::HimeBitonicSort(bool isSorting64Bits)
    {
        mpIndirectArgsBuffer = Buffer::createStructured(12, 22 * 23 / 2, Resource::BindFlags::ShaderResource | Resource::BindFlags::UnorderedAccess, Buffer::CpuAccess::None, nullptr, false);
        mpIndirectArgsBuffer->setName("HimeBitonicSort::DispatchArgs");

        Program::DefineList defineList;
        if (isSorting64Bits)
        {
            defineList.add("BITONICSORT_64BIT", "1");
        }

        mpBitonicIndirectArgs = ComputePass::create(kBitonicIndirectArgsFilename, "main", defineList);
        mpBitonicPreSort = ComputePass::create(kBitonicPreSortFilename, "main", defineList);
        mpBitonicOuterSort = ComputePass::create(kBitonicOuterSortFilename, "main", defineList);
        mpBitonicInnerSort = ComputePass::create(kBitonicInnerSortFilename, "main", defineList);
    }

    HimeBitonicSort::SharedPtr HimeBitonicSort::create(bool isSortingKeyIndexPair)
    {
        return SharedPtr(new HimeBitonicSort(isSortingKeyIndexPair));
    }

    void HimeBitonicSort::sort(RenderContext* pRenderContext, Buffer::SharedPtr pKeyIndexList, uint elementCount, uint counterOffset, bool IsPartiallyPreSorted, bool isAscending)
    {
        PROFILE("Hime bitonic sort");

        if (mpCounterBuffer == nullptr)
        {
            mpCounterBuffer = Buffer::createStructured(sizeof(uint32_t), 1); // FIXME: bug may exists here
            mpCounterBuffer->setName("CounterBuffer");
        }
        mpCounterBuffer->setBlob(&elementCount, 0, sizeof(uint32_t));
        const uint32_t MaxNumElements = pKeyIndexList->getElementCount();
        const uint32_t AlignedMaxNumElements = AlignPowerOfTwo(MaxNumElements);
        const uint32_t MaxIterations = Log2(std::max(2048u, AlignedMaxNumElements)) - 10;

        assert(ElementSizeBytes == 4 || ElementSizeBytes == 8);

        setConstant(mpBitonicIndirectArgs, mpCounterBuffer, counterOffset, isAscending);
        setConstant(mpBitonicPreSort, mpCounterBuffer, counterOffset, isAscending);
        setConstant(mpBitonicOuterSort, mpCounterBuffer, counterOffset, isAscending);
        setConstant(mpBitonicInnerSort, mpCounterBuffer, counterOffset, isAscending);

        // Generate execute indirect arguments
        mpBitonicIndirectArgs.getRootVar()["g_IndirectArgsBuffer"] = mpIndirectArgsBuffer;
        mpBitonicIndirectArgs.getRootVar()["Constants"]["MaxIterations"] = MaxIterations;
        mpBitonicIndirectArgs->execute(pRenderContext, uint3(1, 1, 1));

        // Pre-Sort the buffer up to k = 2048.  This also pads the list with invalid indices
        // that will drift to the end of the sorted list.
        if (!IsPartiallyPreSorted)
        {
            mpBitonicPreSort.getRootVar()["g_SortBuffer"] = pKeyIndexList;
            mpBitonicPreSort->executeIndirect(pRenderContext, mpIndirectArgsBuffer.get(), 0);
        }

        uint32_t IndirectArgsOffset = 12;

        // We have already pre-sorted up through k = 2048 when first writing our list, so
        // we continue sorting with k = 4096.  For unnecessarily large values of k, these
        // indirect dispatches will be skipped over with thread counts of 0.

        for (uint32_t k = 4096; k <= AlignedMaxNumElements; k *= 2)
        {

            for (uint32_t j = k / 2; j >= 2048; j /= 2)
            {
                mpBitonicOuterSort.getRootVar()["Constants"]["k"] = k;
                mpBitonicOuterSort.getRootVar()["Constants"]["j"] = j;
                mpBitonicOuterSort.getRootVar()["g_SortBuffer"] = pKeyIndexList;
                mpBitonicOuterSort->executeIndirect(pRenderContext, mpIndirectArgsBuffer.get(), IndirectArgsOffset);

                IndirectArgsOffset += 12;
            }

            mpBitonicInnerSort.getRootVar()["Constants"]["k"] = k;
            mpBitonicInnerSort.getRootVar()["g_SortBuffer"] = pKeyIndexList;
            mpBitonicInnerSort->executeIndirect(pRenderContext, mpIndirectArgsBuffer.get(), IndirectArgsOffset);

            IndirectArgsOffset += 12;
        }
    }

    void HimeBitonicSort::sort(RenderContext* pRenderContext, Buffer::SharedPtr pKeyIndexList, Buffer::SharedPtr pCounterBuffer, uint counterOffset, bool IsPartiallyPreSorted, bool isAscending)
    {
        PROFILE("Hime bitonic sort");

        const uint32_t MaxNumElements = pKeyIndexList->getElementCount();
        const uint32_t AlignedMaxNumElements = AlignPowerOfTwo(MaxNumElements);
        const uint32_t MaxIterations = Log2(std::max(2048u, AlignedMaxNumElements)) - 10;

        assert(ElementSizeBytes == 4 || ElementSizeBytes == 8);

        setConstant(mpBitonicIndirectArgs, pCounterBuffer, counterOffset, isAscending);
        setConstant(mpBitonicPreSort, pCounterBuffer, counterOffset, isAscending);
        setConstant(mpBitonicOuterSort, pCounterBuffer, counterOffset, isAscending);
        setConstant(mpBitonicInnerSort, pCounterBuffer, counterOffset, isAscending);

        // Generate execute indirect arguments
        mpBitonicIndirectArgs.getRootVar()["g_IndirectArgsBuffer"] = mpIndirectArgsBuffer;
        mpBitonicIndirectArgs.getRootVar()["Constants"]["MaxIterations"] = MaxIterations;
        mpBitonicIndirectArgs->execute(pRenderContext, uint3(1, 1, 1));

        // Pre-Sort the buffer up to k = 2048.  This also pads the list with invalid indices
        // that will drift to the end of the sorted list.
        if (!IsPartiallyPreSorted)
        {
            mpBitonicPreSort.getRootVar()["g_SortBuffer"] = pKeyIndexList;
            mpBitonicPreSort->executeIndirect(pRenderContext, mpIndirectArgsBuffer.get(), 0);
        }

        uint32_t IndirectArgsOffset = 12;

        // We have already pre-sorted up through k = 2048 when first writing our list, so
        // we continue sorting with k = 4096.  For unnecessarily large values of k, these
        // indirect dispatches will be skipped over with thread counts of 0.

        for (uint32_t k = 4096; k <= AlignedMaxNumElements; k *= 2)
        {

            for (uint32_t j = k / 2; j >= 2048; j /= 2)
            {
                mpBitonicOuterSort.getRootVar()["Constants"]["k"] = k;
                mpBitonicOuterSort.getRootVar()["Constants"]["j"] = j;
                mpBitonicOuterSort.getRootVar()["g_SortBuffer"] = pKeyIndexList;
                mpBitonicOuterSort->executeIndirect(pRenderContext, mpIndirectArgsBuffer.get(), IndirectArgsOffset);

                IndirectArgsOffset += 12;
            }

            mpBitonicInnerSort.getRootVar()["Constants"]["k"] = k;
            mpBitonicInnerSort.getRootVar()["g_SortBuffer"] = pKeyIndexList;
            mpBitonicInnerSort->executeIndirect(pRenderContext, mpIndirectArgsBuffer.get(), IndirectArgsOffset);

            IndirectArgsOffset += 12;
        }
    }

    void HimeBitonicSort::setConstant(ComputePass::SharedPtr pComputePass, Buffer::SharedPtr pCounterBuffer, uint32_t counterOffset, bool isAscending)
    {
        pComputePass.getRootVar()["CB1"]["CounterOffset"] = counterOffset;
        pComputePass.getRootVar()["CB1"]["NullItem"] = isAscending ? 0xffffffff : 0;
        pComputePass.getRootVar()["g_CounterBuffer"] = pCounterBuffer;
    }
}
