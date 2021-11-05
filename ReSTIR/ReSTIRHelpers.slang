__exported import Experimental.Scene.Material.StandardMaterial;
__exported import Experimental.Scene.Lights.EmissiveLightSamplerInterface;
__exported import Scene.ShadingData;
__exported import Utils.Color.ColorHelpers;
__exported import ReservoirData;

ReservoirData createEmptyReservoir()
{
    ReservoirData s = {};
    s.lightData = 0;
    s.uvData = 0;
    s.targetPdf = 0;
    s.weightSum = 0;
    s.M = 0;
    s.spatialDistance = 0;
    s.age = 0;
    return s;
}

PackedReservoirData packReservoir(const ReservoirData reservoir)
{
    int2 clampedSpatialDistance = clamp(reservoir.spatialDistance, -ReservoirData::c_MaxDistance, ReservoirData::c_MaxDistance);
    int clampedAge = clamp(reservoir.age, 0, ReservoirData::c_MaxAge);

    PackedReservoirData data;
    data.lightData = reservoir.lightData;
    data.uvData = reservoir.uvData;

    data.distanceAge = 
          ((clampedSpatialDistance.x & ReservoirData::c_DistanceMask) << ReservoirData::c_DistanceXShift) 
        | ((clampedSpatialDistance.y & ReservoirData::c_DistanceMask) << ReservoirData::c_DistanceYShift) 
        | (clampedAge << ReservoirData::c_AgeShift);

    data.M = reservoir.M;

    data.targetPdf = reservoir.targetPdf;
    data.weight = reservoir.weightSum;

    return data;
}

ReservoirData unpackReservoir(const PackedReservoirData data)
{
    ReservoirData res;
    res.lightData = data.lightData;
    res.uvData = data.uvData;
    res.targetPdf = data.targetPdf;
    res.weightSum = data.weight;
    res.M = data.M;
    // Sign extend the shift values
    res.spatialDistance.x = int(data.distanceAge << (32 - ReservoirData::c_DistanceXShift - ReservoirData::c_DistanceChannelBits)) >> (32 - ReservoirData::c_DistanceChannelBits);
    res.spatialDistance.y = int(data.distanceAge << (32 - ReservoirData::c_DistanceYShift - ReservoirData::c_DistanceChannelBits)) >> (32 - ReservoirData::c_DistanceChannelBits);
    res.age = data.distanceAge >> ReservoirData::c_AgeShift;

    // Discard reservoirs that have Inf/NaN
    if (isinf(res.weightSum) || isnan(res.weightSum)) {
        res = createEmptyReservoir();
    }

    return res;
}

void storeReservoir(const ReservoirData reservoir, RWStructuredBuffer<PackedReservoirData> packedResevoirs, uint2 idx, uint2 dim)
{
    uint pointer = idx.y * dim.x + idx.x;
    packedResevoirs[pointer] = packReservoir(reservoir);
}

ReservoirData loadReservoir(RWStructuredBuffer<PackedReservoirData> packedResevoirs, uint2 idx, uint2 dim)
{
    uint pointer = idx.y * dim.x + idx.x;
    return unpackReservoir(packedResevoirs[pointer]);
}

// Adds a new, non-reservoir light sample into the reservoir, returns true if this sample was selected.
// Algorithm (3) from the ReSTIR paper, Streaming RIS using weighted reservoir sampling.
bool streamSample(inout ReservoirData reservoir, uint lightIndex, float2 uv, float random, float targetPdf, float invSourcePdf)
{
    // What's the current weight
    float risWeight = targetPdf * invSourcePdf;

    // Add one sample to the counter
    reservoir.M += 1;

    // Update the weight sum
    reservoir.weightSum += risWeight;

    // Decide if we will randomly pick this sample
    bool selectSample = (random * reservoir.weightSum < risWeight);

    // If we did select this sample, update the relevant data.
    // New samples don't have visibility or age information, we can skip that.
    if (selectSample) 
    {
        reservoir.lightData = lightIndex | ReservoirData::c_LightValidBit;
        reservoir.uvData = uint(saturate(uv.x) * 0xffff) | (uint(saturate(uv.y) * 0xffff) << 16);
        reservoir.targetPdf = targetPdf;
    }

    return selectSample;
}

// Adds `newReservoir` into `reservoir`, returns true if the new reservoir's sample was selected.
// Algorithm (4) from the ReSTIR paper, Combining the streams of multiple reservoirs.
// Normalization - Equation (6) - is postponed until all reservoirs are combined.
bool combineReservoirs(inout ReservoirData reservoir, const ReservoirData newReservoir, float random, float targetPdf)
{
    // What's the current weight (times any prior-step RIS normalization factor)
    float risWeight = targetPdf * newReservoir.weightSum * newReservoir.M;

    // Our *effective* candidate pool is the sum of our candidates plus those of our neighbors
    reservoir.M += newReservoir.M;

    // Update the weight sum
    reservoir.weightSum += risWeight;

    // Decide if we will randomly pick this sample
    bool selectSample = (random * reservoir.weightSum < risWeight);

    // If we did select this sample, update the relevant data
    if (selectSample) 
    {
        reservoir.lightData = newReservoir.lightData;
        reservoir.uvData = newReservoir.uvData;
        reservoir.targetPdf = targetPdf;
        reservoir.spatialDistance = newReservoir.spatialDistance;
        reservoir.age = newReservoir.age;
    }

    return selectSample;
}

// Performs normalization of the reservoir after streaming. Equation (6) from the ReSTIR paper.
void finalizeResampling(inout ReservoirData reservoir, float normalizationNumerator, float normalizationDenominator)
{
    float denominator = reservoir.targetPdf * normalizationDenominator;
    reservoir.weightSum = (denominator == 0.0) ? 0.0 : (reservoir.weightSum * normalizationNumerator) / denominator;
}

float computeTargetPdf(TriangleLightSample ls, ShadingData sd)
{
    StandardMaterial mtl;
    float3 Lr = mtl.eval(sd, ls.dir) * ls.Le;
    return luminance(Lr) / ls.pdf;
}

// Compares two values and returns true if their relative difference is lower than the threshold.
// Zero or negative threshold makes test always succeed, not fail.
bool compareRelativeDifference(float reference, float candidate, float threshold)
{
    return (threshold <= 0) || abs(reference - candidate) <= threshold * reference;
}

// See if we will reuse this neighbor or history sample using
//    edge-stopping functions (e.g., per a bilateral filter).
bool isValidNeighbor(float3 ourNorm, float3 theirNorm, float ourDepth, float theirDepth, float normalThreshold, float depthThreshold)
{
    return (dot(theirNorm.xyz, ourNorm.xyz) >= normalThreshold) && compareRelativeDifference(ourDepth, theirDepth, depthThreshold);
}
