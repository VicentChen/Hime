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
}
