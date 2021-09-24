#include "LightTreeData.slangh"

import Scene.Scene;
import Scene.ShadingData;
import RenderPasses.Shared.PathTracer.LoadShadingData;
import Utils.Sampling.SampleGenerator;

#ifndef CHUNK_SIZE
    // Compile-time error if CHUNK_SIZE is not defined.
    #error CHUNK_SIZE is not defined. Add define in cpp file.
#endif

#ifndef NUM_LIGHT_SAMPLES
    // Compile-time error if NUM_LIGHT_SAMPLES is not defined.
    #error NUM_LIGHT_SAMPLES is not defined. Add define in cpp file.
#endif

struct LightcutHeap
{
    uint nodeId = 0;
    float error = 1e27;
};

cbuffer PerFrameCB
{
    // compute shader parameters
    uint2 dispatchDim;
    uint frameCount;

    // light tree parameters
    uint levelCount;

    // light tree node parameters
    float errorLimit;
    float sceneLightBoundRadius;
}

StructuredBuffer<LightTreeNode> gLightTree;

// float4: triangleIndex, pdf, ??, ??
RWTexture2DArray<float4> gLightIndex;

float computeSquaredDistanceToClosestPoint(float3 p, float3 boundMin, float3 boundMax)
{
    float3 d = min(max(p, boundMin), boundMax) - p;
    return dot(d, d);
}

float computeSquaredDistanceToFarthestPoint(float3 p, float3 boundMin, float3 boundMax)
{
    float3 d = max(abs(boundMin - p), abs(boundMax - p));
    return dot(d, d);
}

float normalizedWeights(float l2_0, float l2_1, float intensGeom0, float intensGeom1)
{
    float ww0 = l2_1 * intensGeom0;
    float ww1 = l2_0 * intensGeom1;
    return ww0 / (ww0 + ww1);
};

float computeMaxDistAlong(float3 p, float3 dir, float3 boundMin, float3 boundMax)
{
    float3 dir_p = dir * p;
    float3 mx0 = dir * boundMin - dir_p;
    float3 mx1 = dir * boundMax - dir_p;
    return max(mx0[0], mx1[0]) + max(mx0[1], mx1[1]) + max(mx0[2], mx1[2]);
}

float computeGeomTermBound(float3 p, float3 N, float3 boundMin, float3 boundMax)
{
    float nrm_max = computeMaxDistAlong(p, N, boundMin, boundMax);
    if (nrm_max <= 0) return 0.0f;
    float3 d = min(max(p, boundMin), boundMax) - p;
    float3 tng = d - dot(d, N) * N;
    float hyp2 = dot(tng, tng) + nrm_max * nrm_max;
    return nrm_max * rsqrt(hyp2);
}

float computeError(const ShadingData sd, int nodeId)
{
    LightTreeNode node = gLightTree[nodeId];

    float dlen2 = computeSquaredDistanceToClosestPoint(sd.posW, node.aabbMinPoint, node.aabbMaxPoint);
    float SR2 = errorLimit * sceneLightBoundRadius;
    SR2 *= SR2;
    if (dlen2 < SR2) dlen2 = SR2; // bound the distance

    float atten = rcp(dlen2);

    atten *= computeGeomTermBound(sd.posW, sd.N, node.aabbMinPoint, node.aabbMaxPoint);
    float colorIntens = length(node.intensity); // FIME: are we using real intensity?

    float res = atten * colorIntens;
    return res;
}

void findCut(const ShadingData sd, out uint lightcuts[NUM_LIGHT_SAMPLES])
{
    uint leafStartIdx = getCurrentLevelNodeStartIdx(levelCount - 1);

    LightcutHeap heap[NUM_LIGHT_SAMPLES] = {};
    for (int i = 0; i < NUM_LIGHT_SAMPLES; i++)
    {
        // FIXME: may be the first node error is not FLT_MAX, but it's ok because we will always replace it.
        heap[0].nodeId = 0;
        heap[0].error = 1e27;
    }

    int numLights = 1;
    while (numLights < NUM_LIGHT_SAMPLES)
    {
        // find max node in heap
        float maxError = -1e10;
        int maxIdx = -1;
        for (int i = 0; i < numLights; i++)
        {
            if (heap[i].error > maxError)
            {
                maxIdx = i;
                maxError = heap[i].error;
            }
        }
        if (maxIdx < 0) break;

        int nodeId = heap[maxIdx].nodeId;
        if (nodeId >= leafStartIdx) break;

        // replace as two child
        int lChildId = getLeftChild(nodeId);
        heap[maxIdx].nodeId = lChildId;
        heap[maxIdx].error = computeError(sd, lChildId);

        int rChildId = getRightChild(nodeId);
        if (gLightTree[rChildId].isBogus()) continue;
        heap[numLights].nodeId = rChildId;
        heap[numLights].error = computeError(sd, rChildId);
        numLights++;
    }

    // copy node back to lightcuts
    for (int i = 0; i < NUM_LIGHT_SAMPLES; i++) lightcuts[i] = heap[i].nodeId;
}

bool firstChildWeight(float3 p, float3 N, inout float prob0, int child0, int child1)
{
	LightTreeNode c0 = gLightTree[child0];
	LightTreeNode c1 = gLightTree[child1];

	float c0_intensity = length(c0.intensity);
	float c1_intensity = length(c1.intensity);

	if (c0_intensity == 0)
	{
		if (c1_intensity == 0) return false;
		prob0 = 0;
		return true;
	}
	else if (c1_intensity == 0)
	{
		prob0 = 1;
		return true;
	}

	float3 c0_boundMin = c0.aabbMinPoint;
	float3 c0_boundMax = c0.aabbMaxPoint;
	float3 c1_boundMin = c1.aabbMinPoint;
	float3 c1_boundMax = c1.aabbMaxPoint;

	// Compute the weights
	float geom0 = computeGeomTermBound(p, N, c0_boundMin, c0_boundMax);
	float geom1 = computeGeomTermBound(p, N, c1_boundMin, c1_boundMax);

    if (geom0 + geom1 == 0) return false;

    if (geom0 == 0)
    {
        prob0 = 0;
        return true;
    }
    else if (geom1 == 0)
    {
        prob0 = 1;
        return true;
    }

    float intensGeom0 = c0_intensity * geom0;
    float intensGeom1 = c1_intensity * geom1;

    float l2_min0;
    float l2_min1;
    l2_min0 = computeSquaredDistanceToClosestPoint(p, c0_boundMin, c0_boundMax);
    l2_min1 = computeSquaredDistanceToClosestPoint(p, c1_boundMin, c1_boundMax);

    float l2_max0 = computeSquaredDistanceToFarthestPoint(p, c0_boundMin, c0_boundMax);
    float l2_max1 = computeSquaredDistanceToFarthestPoint(p, c1_boundMin, c1_boundMax);
    float w_max0 = l2_min0 == 0 && l2_min1 == 0 ? intensGeom0 / (intensGeom0 + intensGeom1) : normalizedWeights(l2_min0, l2_min1, intensGeom0, intensGeom1);
    float w_min0 = normalizedWeights(l2_max0, l2_max1, intensGeom0, intensGeom1);
    prob0 = 0.5 * (w_max0 + w_min0);

    return true;
};

bool traverseLightTree(const ShadingData sd, inout uint nodeId, inout float r, inout float nodeProb)
{
    uint leafStartIdx = getCurrentLevelNodeStartIdx(levelCount - 1);

    bool deadBranch = false;
    while (nodeId < leafStartIdx)
    {
        int lChildId = getLeftChild(nodeId);
        int rChildId = getRightChild(nodeId);
        float prob0;

        if (firstChildWeight(sd.posW, sd.N, prob0, lChildId, rChildId))
        {
            if (r < prob0)
            {
                nodeId = lChildId;
                r /= prob0;
                nodeProb *= prob0;
            }
            else
            {
                nodeId = rChildId;
                r = (r - prob0) / (1 - prob0);
                nodeProb *= (1 - prob0);
            }
        }
        else
        {
            deadBranch = true;
            break;
        }

    }

    return deadBranch;
}

void findLight<S : ISampleGenerator>(const ShadingData sd, const uint lightcuts[NUM_LIGHT_SAMPLES], out float4 lightIndex[NUM_LIGHT_SAMPLES], S sg)
{
    for (int i = 0; i < NUM_LIGHT_SAMPLES; i++)
    {
        float r = sampleNext1D(sg);
        float nodeProb = 1;
        uint nodeId = lightcuts[i];

        bool deadBranch = traverseLightTree(sd, nodeId, r, nodeProb);

        // TODO: find light index and pdf
        if (deadBranch) lightIndex[i] = float4(0, 1e27, 0, 0);
        lightIndex[i] = float4(gLightTree[nodeId].lightIdx, nodeProb, lightcuts[i], 0);
    }
}

[numthreads(CHUNK_SIZE, CHUNK_SIZE, 1)]
void findLightcuts(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint2 launchIdx = dispatchThreadId.xy;
    uint2 launchDim = dispatchDim;
    if (any(launchIdx >= launchDim)) return;

    // random generator
    uint frameSeed = frameCount; // [Hime]TODO: useFixedSeed ? 0 : frameCount;
    SampleGenerator sg = SampleGenerator.create(launchIdx, frameSeed);
    
    ShadingData sd;
    if (loadShadingData(launchIdx, launchDim, gScene.camera, sd))
    {
        uint lightcuts[NUM_LIGHT_SAMPLES];
        findCut(sd, lightcuts);
        float4 lightIndex[NUM_LIGHT_SAMPLES];
        findLight(sd, lightcuts, lightIndex, sg);

        // copy selected light index to output
        for (int i = 0; i < NUM_LIGHT_SAMPLES; i++) gLightIndex[uint3(launchIdx, i)] = lightIndex[i];
    }
}
