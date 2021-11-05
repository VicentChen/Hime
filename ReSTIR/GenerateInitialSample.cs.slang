import Scene.Scene;
import Scene.RayTracingInline;
import RenderPasses.Shared.PathTracer.LoadShadingData;
import Experimental.Scene.Lights.EmissivePowerSampler;
import Utils.Sampling.SampleGenerator;
import ReSTIRHelpers;

#ifndef CHUNK_SIZE
    // Compile-time error if CHUNK_SIZE is not defined.
    #error CHUNK_SIZE is not defined. Add define in cpp file.
#endif

RWStructuredBuffer<PackedReservoirData> gReservoirBuffer;
RWTexture2D<float4> gDebugTexture;

cbuffer PerFrameCB
{
    // Compute shader parameters
    uint2 dispatchDim;
    uint frameCount;
    uint candidateCount;

    // Triangle sampler
    EmissivePowerSampler emissivePowerSampler;

    bool ignoreVisibility;
}

[numthreads(CHUNK_SIZE, CHUNK_SIZE, 1)]
void generateInitialSample(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint2 launchIdx = dispatchThreadId.xy;
    uint2 launchDim = dispatchDim;
    if (any(launchIdx >= launchDim)) return;
    
    ShadingData sd;
    if (loadShadingData(launchIdx, launchDim, gScene.camera, sd))
    {
        uint frameSeed = frameCount; // [Hime]TODO: useFixedSeed ? 0 : frameCount;
        SampleGenerator sg = SampleGenerator.create(launchIdx, frameSeed);
        
        ReservoirData state = createEmptyReservoir();
        TriangleLightSample selectedSample;

        int selectedCount = 0;
        for (int i = 0; i < candidateCount; i++)
        {
            TriangleLightSample ls;
            bool sampled = emissivePowerSampler.sampleLight(sd.posW, sd.N, true, sg, ls);
            if (sampled) // Check whether sampler returns true is necessary, otherwise pdf will be polluted.
            {
                // ls.pdf contains two pdf: trianglePdf and solidAnglePdf, however, we need to recover solidAnglePdf to compute targetPdf.
                float trianglePdf = gScene.lightCollection.fluxData[ls.triangleIndex].flux * emissivePowerSampler._emissivePower.invWeightsSum;
                float invSourcePdf = rcp(trianglePdf); // Light source pdf.
                ls.pdf *= invSourcePdf; // recover solidAnglePdf
                float targetPdf = computeTargetPdf(ls, sd);
                float risRnd = sampleNext1D(sg);

                bool selected = streamSample(state, ls.triangleIndex, ls.uv, risRnd, targetPdf, invSourcePdf);
                if (selected)
                {
                    selectedSample = ls;
                    selectedCount += 1;
                }
            }
        }

        finalizeResampling(state, 1.0, state.M);
        state.M = 1;

        // Inline ray tracing for visibility.
        if (!ignoreVisibility && state.isValid())
        {
            RayDesc ray;
            ray.Origin = sd.posW;
            ray.Direction = normalize(selectedSample.posW - sd.posW);
            ray.TMin = 1e-5f;
            ray.TMax = distance(selectedSample.posW, sd.posW);

            SceneRayQuery<0> sceneRayQuery;
            bool visible = sceneRayQuery.traceVisibilityRay(ray, RAY_FLAG_NONE, 0xFF);
            if (!visible)
            {
                state.spatialDistance = 0;
                state.age = 0;
                state.lightData = 0;
                state.weightSum = 0;
            }
        }

        storeReservoir(state, gReservoirBuffer, launchIdx, launchDim);
    }
}
