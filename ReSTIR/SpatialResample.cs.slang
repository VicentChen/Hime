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

Texture2D<float4> gNormalAndLinearZ;
StructuredBuffer<float2> gNeighborOffsetBuffer;
RWStructuredBuffer<PackedReservoirData> gReservoirBuffer;

cbuffer PerFrameCB
{
    // Compute shader parameters
    uint2 dispatchDim;
    uint frameCount;

    uint neighborOffsetCount;
    uint spatialSampleCount;
    float spatialSampleRadius;

    uint maxHistoryLength;
    uint disocclusionBoostSampleCount;

    float normalThreshold;
    float depthThreshold;
}

ReservoirData resampleSpatialNeighbor(const uint2 launchIdx, const uint2 launchDim, const ShadingData centerSd, const ReservoirData centerReservoir, inout SampleGenerator sg)
{
    ReservoirData state = createEmptyReservoir();

    // This is the weight we'll use (instead of 1/M) to make our estimate unbaised (see paper).
    float normalizationWeight = 1.0f;

    // Since we're using our bias correction scheme, we need to remember which light selection we made
    int selected = -1;

    uint selectedLight = 0;

    if (centerReservoir.isValid())
    {
        selectedLight = centerReservoir.getLightIndex();
    }

    combineReservoirs(state, centerReservoir, /* random = */ 0.5f, centerReservoir.targetPdf);

    uint startIdx = sampleNext1D(sg) * (neighborOffsetCount - 1);
    
    int numSpatialSamples = spatialSampleCount;
    if(centerReservoir.M < maxHistoryLength)
        numSpatialSamples = max(disocclusionBoostSampleCount, numSpatialSamples);

    // Clamp the sample count at 32 to make sure we can keep the neighbor mask in an uint (cachedResult)
    numSpatialSamples = min(numSpatialSamples, 32);

    float4 centerNormalAndLinearZ = gNormalAndLinearZ[launchIdx];

    // Walk the specified number of neighbors, resampling using RIS
    for (int i = 0; i < numSpatialSamples; ++i)
    {
        // Get screen-space location of neighbor
        uint sampleIdx = (startIdx + i) & (neighborOffsetCount - 1);
        int2 spatialOffset = int2(gNeighborOffsetBuffer[sampleIdx].xy * spatialSampleRadius);
        int2 neighborIdx = launchIdx + spatialOffset;
        if (any(neighborIdx < 0) || any(neighborIdx >= launchDim)) continue;

        // Edge stopping function to cull invalid neighbors.
        float4 neighborNomralAndLinearZ = gNormalAndLinearZ[neighborIdx];
        if (!isValidNeighbor(
            centerNormalAndLinearZ.xyz, neighborNomralAndLinearZ.xyz, 
            centerNormalAndLinearZ.w, neighborNomralAndLinearZ.w,
            normalThreshold, depthThreshold))
            continue;

        // [Hime]TODO: Check whether material is similar.

        ReservoirData neighborReservoir = loadReservoir(gReservoirBuffer, neighborIdx, launchDim);
        neighborReservoir.spatialDistance += spatialOffset;

        // Load that neighbor's RIS state, do resampling
        if (neighborReservoir.isValid())
        {   
            uint candidateLight = neighborReservoir.getLightIndex();
            
            TriangleLightSample candidateLightSample;
            bool sampled = sampleTriangle(centerSd.posW, candidateLight, neighborReservoir.getSampleUV(), candidateLightSample);
            if (sampled)
            {
                float neighborWeight = computeTargetPdf(candidateLightSample, centerSd);
                if (combineReservoirs(state, neighborReservoir, sampleNext1D(sg), neighborWeight))
                {
                    selected = i;
                    selectedLight = candidateLight;
                }
            }
        }
    }

    // [Hime]TODO: Bias correction.

    finalizeResampling(state, 1.0, state.M);
    return state;
}


[numthreads(CHUNK_SIZE, CHUNK_SIZE, 1)]
void spatialResample(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint2 launchIdx = dispatchThreadId.xy;
    uint2 launchDim = dispatchDim;
    if (any(launchIdx >= launchDim)) return;

    ShadingData centerSd;
    if (loadShadingData(launchIdx, launchDim, gScene.camera, centerSd))
    {
        uint frameSeed = frameCount; // [Hime]TODO: useFixedSeed ? 0 : frameCount;
        SampleGenerator sg = SampleGenerator.create(launchIdx, frameSeed);

        ReservoirData centerReservoir = loadReservoir(gReservoirBuffer, launchIdx, launchDim);
        ReservoirData spatialReservoir = resampleSpatialNeighbor(launchIdx, launchDim, centerSd, centerReservoir, sg);
        storeReservoir(spatialReservoir, gReservoirBuffer, launchIdx, launchDim);
    }
}
