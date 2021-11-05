import Scene.Scene;
import Scene.ShadingData;
import Experimental.Scene.Lights.EmissiveLightSamplerHelpers;
import RenderPasses.Shared.PathTracer.LoadShadingData;
import Utils.Sampling.SampleGenerator;
import ReSTIRHelpers;

#ifndef CHUNK_SIZE
    // Compile-time error if CHUNK_SIZE is not defined.
    #error CHUNK_SIZE is not defined. Add define in cpp file.
#endif

Texture2D<float2> gMotionVector;
Texture2D<float4> gPrevNormalAndLinearZ;
RWStructuredBuffer<PackedReservoirData> gPrevReservoirBuffer;
RWStructuredBuffer<PackedReservoirData> gCurrReservoirBuffer;

cbuffer PerFrameCB
{
    // Compute shader parameters
    uint2 dispatchDim;
    uint frameCount;

    uint maxHistoryLength;
    uint temporalNeighborCandidateCount;
    float boilingFilterStrength;

    float normalThreshold;
    float depthThreshold;
};

#define BOILING_FILTER_MIN_LANE_COUNT 8
groupshared float sWeights[(CHUNK_SIZE * CHUNK_SIZE) / BOILING_FILTER_MIN_LANE_COUNT];
groupshared uint sCount[(CHUNK_SIZE * CHUNK_SIZE) / BOILING_FILTER_MIN_LANE_COUNT];

void boilingFilter(uint2 launchIndex, uint2 LocalIndex, float filterStrength, inout ReservoirData state)
{
    float boilingFilterMultiplier = 10.f / clamp(filterStrength, 1e-6, 1.0) - 9.f;

    // Start with average nonzero weight within the wavefront
    float waveWeight = WaveActiveSum(state.weightSum);
    uint waveCount = WaveActiveCountBits(state.weightSum > 0);

    // Store the results of each wavefront into shared memory
    uint linearThreadIndex = LocalIndex.x + LocalIndex.y * CHUNK_SIZE;
    uint waveIndex = linearThreadIndex / WaveGetLaneCount();

    if (WaveIsFirstLane())
    {
        sWeights[waveIndex] = waveWeight;
        sCount[waveIndex] = waveCount;
    }

    GroupMemoryBarrierWithGroupSync();

    // Reduce the per-wavefront averages into a global average using one wavefront
    if (linearThreadIndex < (CHUNK_SIZE * CHUNK_SIZE) / WaveGetLaneCount())
    {
        waveWeight = sWeights[linearThreadIndex];
        waveCount = sCount[linearThreadIndex];

        waveWeight = WaveActiveSum(waveWeight);
        waveCount = WaveActiveSum(waveCount);

        if (linearThreadIndex == 0)
        {
            sWeights[0] = (waveCount > 0) ? (waveWeight / float(waveCount)) : 0.0;
        }
    }

    GroupMemoryBarrierWithGroupSync();

    // Read the per-group average and apply the threshold
    float averageNonzeroWeight = sWeights[0];
    if (state.weightSum > averageNonzeroWeight * boilingFilterMultiplier)
    {
        state = createEmptyReservoir();
    }
}

ReservoirData resampleTemporalNeighbor(const uint2 launchIdx, const uint2 launchDim, const ShadingData currSd, const ReservoirData currReservoir, inout SampleGenerator sg)
{
    int historyLimit = min(ReservoirData::c_MaxM, maxHistoryLength * currReservoir.M);

    int selectedLightPrevID = -1;

    if (currReservoir.isValid())
    {
        selectedLightPrevID = currReservoir.getLightIndex();
    }

    ReservoirData state = createEmptyReservoir();
    combineReservoirs(state, currReservoir, /* random = */ 0.5, currReservoir.targetPdf);

    // Backproject this pixel to last frame
    int2 prevPos = int2(float2(launchIdx) + gMotionVector[launchIdx].xy * launchDim + float2(0.5,0.5));
    float4 prevNormalAndLinearZ = gPrevNormalAndLinearZ[prevPos];

    bool foundNeighbor = false;
    const float radius = 4;
    int2 spatialOffset = 0;
    int2 temporalIdx = int2(0, 0);

    // Try to find a matching surface in the neighborhood of the reprojected pixel
    for(int i = 0; i < temporalNeighborCandidateCount; i++)
    {
        int2 offset = 0;
        if(i > 0) offset = int2((sampleNext2D(sg) - 0.5) * radius);

        int2 temporalNeighborIdx = prevPos + offset;
        if (any(temporalNeighborIdx < 0) || any(temporalNeighborIdx >= launchDim)) continue;

        // Test surface similarity, discard the sample if the surface is too different.
        float4 temporalNeighborNomralAndLinearZ = gPrevNormalAndLinearZ[temporalNeighborIdx];
        if (!isValidNeighbor(
            prevNormalAndLinearZ.xyz, temporalNeighborNomralAndLinearZ.xyz, 
            prevNormalAndLinearZ.w, temporalNeighborNomralAndLinearZ.w,
            normalThreshold, depthThreshold))
            continue;

        spatialOffset = offset;
        temporalIdx = temporalNeighborIdx;
        foundNeighbor = true;
        break;
    }

    bool selectedPreviousSample = false;
    uint previousM = 0;

    if (foundNeighbor)
    {
        // Resample the previous frame sample into the current reservoir, but reduce the light's weight
        // according to the bilinear weight of the current pixel
        ReservoirData prevReservoir = loadReservoir(gPrevReservoirBuffer, temporalIdx, launchDim);
        prevReservoir.M = min(prevReservoir.M, historyLimit);
        prevReservoir.spatialDistance += spatialOffset;
        prevReservoir.age += 1;

        uint originalPrevLightID = prevReservoir.getLightIndex();

        // [Hime]TODO: Convert to previous frame's light index.

        float weightAtCurrent = 0;
        if (prevReservoir.isValid())
        {
            const uint candidateLight = prevReservoir.getLightIndex();

            TriangleLightSample candidateLightSample;
            bool sampled = sampleTriangle(currSd.posW, candidateLight, prevReservoir.getSampleUV(), candidateLightSample);
            if (sampled)
            {
                weightAtCurrent = computeTargetPdf(candidateLightSample, currSd);
            }
        }

        bool sampleSelected = combineReservoirs(state, prevReservoir, sampleNext1D(sg), weightAtCurrent);
        if(sampleSelected)
        {
            selectedPreviousSample = true;
            selectedLightPrevID = int(originalPrevLightID);
        }
    }

    finalizeResampling(state, 1.0, state.M);
    return state;
}

[numthreads(CHUNK_SIZE, CHUNK_SIZE, 1)]
void temporalResample(uint3 dispatchThreadId : SV_DispatchThreadID, uint3 localThreadId : SV_GroupThreadID)
{
    uint2 launchIdx = dispatchThreadId.xy;
    uint2 launchDim = dispatchDim;
    // Note: never early return here, otherwise wave intrinsics will not be executed at the bottom of the screen. 

    ReservoirData temporalReservoir = createEmptyReservoir();

    ShadingData currSd;
    if (loadShadingData(launchIdx, launchDim, gScene.camera, currSd))
    {
        uint frameSeed = frameCount; // [Hime]TODO: useFixedSeed ? 0 : frameCount;
        SampleGenerator sg = SampleGenerator.create(launchIdx, frameSeed);

        ReservoirData currReservoir = loadReservoir(gCurrReservoirBuffer, launchIdx, launchDim);
        temporalReservoir = resampleTemporalNeighbor(launchIdx, launchDim, currSd, currReservoir, sg);
    }

#if ENABLE_BOILING_FILTER
    boilingFilter(launchIdx, localThreadId.xy, boilingFilterStrength, temporalReservoir);
#endif

    storeReservoir(temporalReservoir, gCurrReservoirBuffer, launchIdx, launchDim);
}
