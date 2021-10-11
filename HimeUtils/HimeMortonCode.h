#pragma once
#include "Falcor.h"
#include "HimeMath.h"

namespace Falcor
{
    inline float3 computePosByMortonCode(const uint mortonCode, const float quantLevels, const AABB& sceneBound)
    {
        float3 quantPos = float3(deinterleave_uint3(mortonCode)) / quantLevels;
        float3 pos = quantPos * sceneBound.extent() + sceneBound.minPoint;
        return pos;
    }

    inline AABB computeAABBByMortonCode(const uint mortonCode, const uint prefixLength, const float quantLevels, const AABB& sceneBound)
    {
        uint maskMin = 0xFFFFFFFF << (30 - prefixLength);
        uint mortonCodeMin = mortonCode & (maskMin);
        uint maskMax = 1 << (30 - prefixLength);
        uint mortonCodeMax = mortonCode | (maskMax - 1);

        float3 minPos = computePosByMortonCode(mortonCodeMin, quantLevels, sceneBound);
        float3 maxPos = computePosByMortonCode(mortonCodeMax, quantLevels, sceneBound);

        return AABB(minPos, maxPos);
    }

    inline float3 computePosByMortonCode(const uint mortonCode, const uint prefixLength, const float quantLevels, const AABB& sceneBound)
    {
        uint maskMin = 0xFFFFFFFF << (30 - prefixLength);
        uint mortonCodeMin = mortonCode & (maskMin);
        uint maskMax = 1 << (30 - prefixLength);
        uint mortonCodeMax = mortonCode | (maskMax - 1);

        float3 minPos = computePosByMortonCode(mortonCodeMin, quantLevels, sceneBound);
        float3 maxPos = computePosByMortonCode(mortonCodeMax, quantLevels, sceneBound);

        return (minPos + maxPos) * 0.5f;
    }
}
