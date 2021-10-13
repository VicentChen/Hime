__exported import HimeMath;
__exported import Utils.Math.AABB;

cbuffer PerFrameMortonCodeCB
{
    uint quantLevels; // morton code dimension
    AABB sceneBound;  // morton code scene bound(cubic scene bound)
}

static const uint kInvalidMortonCode = 0xFFFFFFFF;

bool isValidMortonCode(uint mortonCode)
{
    // we define the top 2 bits of a valid morton code are all 0
    return (mortonCode & 0xC0000000) == 0;
}

uint computeMortonCodeByPos(const float3 pos)
{
    //normalize position to [0,1]
    float3 normPos = (pos - sceneBound.minPoint) / sceneBound.extent();
    uint3 quantPos = min(max(0, uint3(normPos * quantLevels)), quantLevels - 1);
    uint mortonCode = interleave_30bits_uint3(quantPos);
    return mortonCode;
}

float3 computePosByMortonCode(uint mortonCode)
{
    float3 quantPos = float3(deinterleave_30bits_uint3(mortonCode)) / quantLevels;
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

/** Check if two morton codes have same prefix
    TODO: rename to hasSameMortonCodePrefix
*/
bool hasSamePrefix(uint num1, uint num2, uint prefixLength)
{
    uint mask = 0xFFFFFFFF << (30 - prefixLength);
    return (num1 & mask) == (num2 & mask);
}
