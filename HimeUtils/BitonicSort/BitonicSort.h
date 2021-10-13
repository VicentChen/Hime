#pragma once

#include "Falcor.h"
#include "../HimeUtils.h"

namespace Falcor
{
    /** Migration of bitonic sort for key-value pair implemented by Microsoft.

        BitonicSort provided by Falcor only supports sorting 32-bit values. This migration
        supports sorting key-value(or key-index) pair, which means we can sort objects with GPU.

        Reference: https://github.com/microsoft/DirectX-Graphics-Samples
    */
    class HIME_UTILS_DECL HimeBitonicSort : public std::enable_shared_from_this<HimeBitonicSort>
    {
    public:
        using SharedPtr = std::shared_ptr<HimeBitonicSort>;
        using SharedConstPtr = std::shared_ptr<const HimeBitonicSort>;
        virtual ~HimeBitonicSort() = default;

        /** Create a new bitonic sort object.
           \param isSortingKeyIndexPair False: sort keys only. True: sort key-index pair.
           \return New object, or throws an exception on error.
        */
        static SharedPtr create(bool isSortingKeyIndexPair);

        void sort(RenderContext* pRenderContext, Buffer::SharedPtr pKeyIndexList, uint elementCount, uint counterOffset = 0, bool IsPartiallyPreSorted = false, bool isAscending = true);
        void sort(RenderContext* pRenderContext, Buffer::SharedPtr pKeyIndexList, Buffer::SharedPtr pCounterBuffer, uint counterOffset, bool IsPartiallyPreSorted = false, bool isAscending = true);

    private:
        HimeBitonicSort(bool isSorting64Bits = false);
        void setConstant(ComputePass::SharedPtr pComputePass, Buffer::SharedPtr pCounterBuffer, uint32_t counterOffset, bool isAscending);

        __forceinline uint8_t Log2(uint64_t value)
        {
            unsigned long mssb; // most significant set bit
            unsigned long lssb; // least significant set bit

            // If perfect power of two (only one set bit), return index of bit.  Otherwise round up
            // fractional log by adding 1 to most signicant set bit's index.
            if (_BitScanReverse64(&mssb, value) > 0 && _BitScanForward64(&lssb, value) > 0)
                return uint8_t(mssb + (mssb == lssb ? 0 : 1));
            else
                return 0;
        }

        template <typename T> __forceinline T AlignPowerOfTwo(T value)
        {
            return value == 0 ? 0 : 1 << Log2(value);
        }

        Buffer::SharedPtr mpIndirectArgsBuffer;
        Buffer::SharedPtr mpCounterBuffer;

        ComputePass::SharedPtr mpBitonicIndirectArgs;
        ComputePass::SharedPtr mpBitonicPreSort;
        ComputePass::SharedPtr mpBitonicOuterSort;
        ComputePass::SharedPtr mpBitonicInnerSort;
    };
}
