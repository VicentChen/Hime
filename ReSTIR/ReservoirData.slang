#pragma once

struct ReservoirData
{
    // Light index (bits 0..30) and validity bit (31)
    uint lightData = 0;

    // Sample UV encoded in 16-bit fixed point format
    uint uvData = 0;

    // Overloaded: represents RIS weight sum during streaming,
    // then reservoir weight (inverse PDF) after FinalizeResampling
    float weightSum = 0.0f;

    // Target PDF of the selected sample
    float targetPdf = 0.0f;

    // Number of samples considered for this reservoir
    uint M = 0;

    // Screen-space distance between the current location of the reservoir
    // and the location where the visibility information was generated,
    // minus the motion vectors applied in temporal resampling
    int2 spatialDistance = int2(0, 0);

    // How many frames ago the visibility information was generated
    uint age = 0;

    static const uint c_MaxM = 0x3fff;

    // Encoding helper constants for distanceAge
    static const uint c_DistanceChannelBits = 8;
    static const uint c_DistanceXShift = 0;
    static const uint c_DistanceYShift = 8;
    static const uint c_AgeShift = 16;
    static const uint c_MaxAge = 0xff;
    static const uint c_DistanceMask = (1u << c_DistanceChannelBits) - 1;
    static const  int c_MaxDistance = int((1u << (c_DistanceChannelBits - 1)) - 1);

    // Light index helpers
    static const uint c_LightValidBit = 0x80000000;
    static const uint c_LightIndexMask = 0x7FFFFFFF;

    bool isValid() { return lightData != 0; }
    uint getLightIndex() { return lightData & c_LightIndexMask; }
    float2 getSampleUV() { return float2(uvData & 0xffff, uvData >> 16) / float(0xffff); }
    float getInvPdf() { return weightSum; }
};

struct PackedReservoirData
{
    uint lightData = 0;
    uint uvData = 0;
    uint distanceAge = 0;
    uint M = 0;
    float targetPdf = 0.0f;
    float weight = 0.0f;
};
