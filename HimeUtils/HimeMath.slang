// [Hime]TODO: rename these functions.
//       Slang or HLSL will automatically cast basic variable types. (Eg: uint casted to uint2)
//       Which is extremely hard to debug.

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

/** 32-bit bit interleave (Morton code).
    "Insert" two 0 bits after each of the 10 low bits of x.
    \param[in] x 10-bit integer.
    \return 32-bit value.
*/
uint interleave_30bits_uint(uint x)
{
    x &= 0x000003ff;                  // x = ---- ---- ---- ---- ---- --98 7654 3210
    x = (x ^ (x << 16)) & 0xff0000ff; // x = ---- --98 ---- ---- ---- ---- 7654 3210
    x = (x ^ (x <<  8)) & 0x0300f00f; // x = ---- --98 ---- ---- 7654 ---- ---- 3210
    x = (x ^ (x <<  4)) & 0x030c30c3; // x = ---- --98 ---- 76-- --54 ---- 32-- --10
    x = (x ^ (x <<  2)) & 0x09249249; // x = ---- 9--8 --7- -6-- 5--4 --3- -2-- 1--0
    return x;
}

/** 32-bit bit de-interleave (inverse Morton code).
    Inverse of interleave_32bit - "delete" all bits not at positions divisible by 3
    \param[in] 30-bit interleaved morton code.
    \return 10-bit unsigned integer.
*/
uint deinterleave_30bits_uint(uint x)
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
uint interleave_30bits_uint3(uint3 v)
{
    // TODO: vectorize this operation
    uint vx = interleave_30bits_uint(v.x);
    uint vy = interleave_30bits_uint(v.y);
    uint vz = interleave_30bits_uint(v.z);
    return vx * 4 + vy * 2 + vz;
}

/** 32-bit bit de-interleave (inverse Morton code).
    Inverse of interleave_32bit - "delete" all bits not at positions divisible by 3
    \param[in] 30-bit interleaved morton code.
    \return 3 uints.
*/
uint3 deinterleave_30bits_uint3(uint x)
{
    uint3 v = 0;
    v.z = deinterleave_30bits_uint(x);
    v.y = deinterleave_30bits_uint(x >> 1);
    v.x = deinterleave_30bits_uint(x >> 2);
    return v;
}
