#pragma once

#include "Utils/HostDeviceShared.slangh"

namespace Falcor
{
    /** Find next 2^n for any integer.
        \param x integer, be care of overflow
        \return next 2^n
    */
    inline int nextPow2(int x)
    {
        char bitCount = 0;
        for (bitCount = 0; x > 0; bitCount++) x >>= 1;
        return 1 << bitCount;
    }

    /** log2 for integers
        http://graphics.stanford.edu/~seander/bithacks.html#IntegerLog
     */
    inline uint uintLog2(uint v)
    {
        uint r; // result of log2(v) will go here
        uint shift;
        r = (v > 0xFFFF) << 4; v >>= r;
        shift = (v > 0xFF) << 3; v >>= shift; r |= shift;
        shift = (v > 0xF) << 2; v >>= shift; r |= shift;
        shift = (v > 0x3) << 1; v >>= shift; r |= shift;
        r |= (v >> 1);
        return r;
    }

    inline uint deinterleave_32bit(uint x)
    {
        x &= 0x09249249;                  // x = ---- 9--8 --7- -6-- 5--4 --3- -2-- 1--0
        x = (x ^ (x >> 2)) & 0x030c30c3;  // x = ---- --98 ---- 76-- --54 ---- 32-- --10
        x = (x ^ (x >> 4)) & 0x0300f00f;  // x = ---- --98 ---- ---- 7654 ---- ---- 3210
        x = (x ^ (x >> 8)) & 0xff0000ff;  // x = ---- --98 ---- ---- ---- ---- 7654 3210
        x = (x ^ (x >> 16)) & 0x000003ff; // x = ---- ---- ---- ---- ---- --98 7654 3210
        return x;
    }

    inline uint3 deinterleave_uint3(uint x)
    {
        uint3 v;
        v.z = deinterleave_32bit(x);
        v.y = deinterleave_32bit(x >> 1);
        v.x = deinterleave_32bit(x >> 2);
        return v;
    }

    inline uint interleave_32bit(uint x)
    {
        x &= 0x000003ff;                  // x = ---- ---- ---- ---- ---- --98 7654 3210
        x = (x ^ (x << 16)) & 0xff0000ff; // x = ---- --98 ---- ---- ---- ---- 7654 3210
        x = (x ^ (x << 8)) & 0x0300f00f; // x = ---- --98 ---- ---- 7654 ---- ---- 3210
        x = (x ^ (x << 4)) & 0x030c30c3; // x = ---- --98 ---- 76-- --54 ---- 32-- --10
        x = (x ^ (x << 2)) & 0x09249249; // x = ---- 9--8 --7- -6-- 5--4 --3- -2-- 1--0
        return x;
    }

    inline uint interleave_uint3(uint3 v)
    {
        uint vx = interleave_32bit(v.x);
        uint vy = interleave_32bit(v.y);
        uint vz = interleave_32bit(v.z);
        return vx * 4 + vy * 2 + vz;
    }
}
