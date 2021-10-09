__exported import HimeMath;
__exported import Utils.Math.AABB;

cbuffer PerFrameMortonCodeCB
{
    uint quantLevels; // morton code dimension
    AABB sceneBound;  // morton code scene bound(cubic scene bound)
}

uint computeMortonCodeByPos(const float3 pos)
{
    //normalize position to [0,1]
    float3 normPos = (pos - sceneBound.minPoint) / sceneBound.extent();
    uint3 quantPos = min(max(0, uint3(normPos * quantLevels)), quantLevels - 1);
    uint mortonCode = interleave_uint3(quantPos);
    return mortonCode;
}

float3 computePosByMortonCode(uint mortonCode)
{
    float3 quantPos = float3(deinterleave_uint3(mortonCode)) / quantLevels;
    float3 pos = quantPos * sceneBound.extent() + sceneBound.minPoint;
    return pos;
}

float3 computePosByMortonCode(uint mortonCode, uint prefixLength)
{
    uint maskMin = 0xFFFFFFFF << (30 - prefixLength);
    uint mortonCodeMin = mortonCode & (maskMin);
    uint maskMax = 1 << (30 - prefixLength);
    uint mortonCodeMax = mortonCode | (maskMax - 1);
    
    float3 minPos = computePosByMortonCode(mortonCodeMin);
    float3 maxPos = computePosByMortonCode(mortonCodeMax);

    return (minPos + maxPos) * 0.5f;
}

AABB computeAABBByMortonCode(uint mortonCode, uint prefixLength)
{
    uint maskMin = 0xFFFFFFFF << (30 - prefixLength);
    uint mortonCodeMin = mortonCode & (maskMin);
    uint maskMax = 1 << (30 - prefixLength);
    uint mortonCodeMax = mortonCode | (maskMax - 1);

    float3 minPos = computePosByMortonCode(mortonCodeMin);
    float3 maxPos = computePosByMortonCode(mortonCodeMax);

    AABB aabb;
    aabb.minPoint = minPos;
    aabb.maxPoint = maxPos;
    return aabb;
}
