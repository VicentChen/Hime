__exported import Utils.Math.AABB;
__exported import Utils.Math.BitTricks;

cbuffer PerFrameMortonCodeCB
{
    uint quantLevels; // morton code dimension
    AABB sceneBound;  // morton code scene bound(cubic scene bound)
}

/** log2 for integers
    From: http://graphics.stanford.edu/~seander/bithacks.html#IntegerLog
    \param[in] v integer
    \return log2(v)
 */
uint uintLog2(uint v)
{
	uint r; // result of log2(v) will go here
	uint shift;
	    r = (v > 0xFFFF);     r =     r << 4; v >>= r;
	shift = (v >   0xFF); shift = shift << 3; v >>= shift; r |= shift;
	shift = (v >    0xF); shift = shift << 2; v >>= shift; r |= shift;
	shift = (v >    0x3); shift = shift << 1; v >>= shift; r |= shift;
	r |= (v >> 1);
	return r;
}

/** 32-bit bit de-interleave (inverse Morton code).
    Inverse of interleave_32bit - "delete" all bits not at positions divisible by 3
    \param[in] 30-bit interleaved morton code.
    \return 10-bit unsigned integer.
*/
uint deinterleave_32bit(uint x)
{
    x &= 0x09249249;                  // x = ---- 9--8 --7- -6-- 5--4 --3- -2-- 1--0
    x = (x ^ (x >>  2)) & 0x030c30c3; // x = ---- --98 ---- 76-- --54 ---- 32-- --10
    x = (x ^ (x >>  4)) & 0x0300f00f; // x = ---- --98 ---- ---- 7654 ---- ---- 3210
    x = (x ^ (x >>  8)) & 0xff0000ff; // x = ---- --98 ---- ---- ---- ---- 7654 3210
    x = (x ^ (x >> 16)) & 0x000003ff; // x = ---- ---- ---- ---- ---- --98 7654 3210
    return x;
}

/** 32-bit bit interleave (Morton code).
    "Insert" two 0 bits after each of the 10 low bits of x.
    \param[in] v 3 10-bit uint.
    \return 32-bit value.
*/
uint interleave_uint3(uint3 v)
{
    // TODO: vectorize this operation
    uint vx = interleave_32bit(v.x);
    uint vy = interleave_32bit(v.y);
    uint vz = interleave_32bit(v.z);
    return vx * 4 + vy * 2 + vz;
}

/** 32-bit bit de-interleave (inverse Morton code).
    Inverse of interleave_32bit - "delete" all bits not at positions divisible by 3
    \param[in] 30-bit interleaved morton code.
    \return 3 uints.
*/
uint3 deinterleave_uint3(uint x)
{
    uint3 v = 0;
    v.z = deinterleave_32bit(x);
    v.y = deinterleave_32bit(x >> 1);
    v.x = deinterleave_32bit(x >> 2);
    return v;
}

/** Check if two morton codes have same prefix
*/
bool hasSamePrefix(uint mortonCode1, uint mortonCode2, uint prefixLength)
{
    uint mask = 0xFFFFFFFF << (30 - prefixLength);
    return (mortonCode1 & mask) == (mortonCode2 & mask);
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