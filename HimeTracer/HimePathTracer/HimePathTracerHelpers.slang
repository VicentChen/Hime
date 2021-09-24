#include "Utils/Math/MathConstants.slangh"

// TODO: Which ones need __exported
import Scene.Scene;
import Scene.ShadingData;
__exported import EmissiveTriangleSampler;
__exported import RenderPasses.Shared.PathTracer.PathTracerHelpers;

Texture2DArray<float4> gEmissiveTriangle;

/** Samples a light source in the scene.
    This function first stochastically selects a type of light source to sample,
    and then calls that the sampling function for the chosen light type.
    \param[in] params PathTracer parameters.
    \param[in] envMapSampler Environment map sampler.
    \param[in] emissiveSampler Emissive triangle sampler.
    \param[in] sd Shading data.
    \param[in] mtl The material at the shading point.
    \param[in] rayOrigin Ray origin for the shadow ray.
    \param[in] numSamples Total number of light samples that will be taken. This is needed for the MIS weighting.
    \param[in] i The index of shadow ray.
    \param[in,out] sg SampleGenerator object.
    \param[out] ls Generated light sample. Only valid if true is returned.
    \return True if a sample was generated, false otherwise.
*/
bool sampleEmissiveTriangle<S : ISampleGenerator, Material : IMaterial>(const PathTracerParams params, const EnvMapSampler envMapSampler, const EmissiveTriangleSampler emissiveSampler, const ShadingData sd, const Material mtl, const float3 rayOrigin, const uint numSamples, const uint i, inout S sg, out SceneLightSample ls)
{
    // Set relative probabilities of the different sampling techniques.
    // TODO: These should use estimated irradiance from each light type. Using equal probabilities for now.
    // TODO: Refactor the selection pdf code the helpers above for evaluating the individual probabilities.
    float p[3];
    p[0] = kUseEnvLight ? 1.f : 0.f;
    p[1] = kUseAnalyticLights ? 1.f : 0.f;
    p[2] = kUseEmissiveLights ? 1.f : 0.f;

    // Normalize probabilities. Early out if zero.
    float sum = p[0] + p[1] + p[2];
    if (sum == 0.f) return false;

    float invSum = 1.f / sum;
    p[0] *= invSum;
    p[1] *= invSum;
    p[2] *= invSum;

    // Sample based on uniform random number. Rescale u to [0,1) afterwards.
    float invPdf = 0.f;
    float u = sampleNext1D(sg);

    // We use explicit checks for which light types are enabled so that the compiler
    // can remove the unused code. It won't otherwise since u is unknown at compile time.
    if (kUseEnvLight)
    {
        if (u < p[0])
        {
            float selectionPdf = p[0];

            // Sample environment map.
            EnvMapSample lightSample;
            envMapSampler.sample(sampleNext2D(sg), lightSample);

            // Reject sample if lower hemisphere.
            if (dot(sd.N, lightSample.dir) < kMinCosTheta) return false;

            // Evaluate MIS with BRDF sampling as the other sampling strategy.
            float pdf = selectionPdf * lightSample.pdf;
            float misWeight = 1.f;
            if (kUseMIS && pdf > 0.f)
            {
                float brdfPdf = evalPdfScatter(sd, mtl, lightSample.dir);
                misWeight = evalMIS(params, numSamples, pdf, 1, brdfPdf);
            }

            // Setup returned sample.
            ls.rayDir = ls.dir = lightSample.dir;
            ls.rayDistance = ls.distance = kRayTMax;
            ls.pdf = pdf;
            ls.Li = pdf > 0.f ? lightSample.Le * misWeight / (pdf * numSamples) : float3(0);

            return true;
        }
        u -= p[0];
    }

    if (kUseAnalyticLights)
    {
        if (u < p[1])
        {
            // Sample analytic light source selected uniformly from the light list.
            // TODO: Sample based on estimated contributions as pdf.
            u /= p[1]; // Rescale to [0,1)
            uint lightCount = gScene.getLightCount();
            uint lightIndex = min(uint(u * lightCount), lightCount - 1);
            float selectionPdf = p[1] / lightCount; // TODO: Precompute 1.f/lightCount in cbuffer

            // Sample local light source.
            AnalyticLightSample lightSample;
            bool valid = sampleLight(rayOrigin, gScene.getLight(lightIndex), sg, lightSample);

            // Reject sample if lower hemisphere.
            if (!valid || dot(sd.N, lightSample.dir) < kMinCosTheta) return false;

            // Setup returned sample.
            // Analytic lights do not currently have a geometric representation in the scene.
            // Do not worry about adjusting the ray to avoid self-intersections at the light.
            ls.rayDir = ls.dir = lightSample.dir;
            ls.rayDistance = ls.distance = lightSample.distance;
            ls.pdf = selectionPdf * lightSample.pdf;
            ls.Li = lightSample.Li / (selectionPdf * numSamples);

            return true;
        }
        u -= p[1];
    }

    if (kUseEmissiveLights)
    {
        //if (u < p[2]) // Always true
        {
            float selectionPdf = p[2];

            // Sample emissive lights.
            float4 emissiveTriangle = gEmissiveTriangle[uint3(DispatchRaysIndex().xy, i)];
            uint triangleIdx = emissiveTriangle.x;
            float trianglePdf = emissiveTriangle.y;

            TriangleLightSample lightSample;
            bool valid = emissiveSampler.sampleLight(rayOrigin, sd.N, triangleIdx, trianglePdf, sg, lightSample);

            // Reject sample if lower hemisphere.
            if (!valid || dot(sd.N, lightSample.dir) < kMinCosTheta) return false;

            // Evaluate MIS with BRDF sampling as the other sampling strategy.
            float pdf = selectionPdf * lightSample.pdf;
            float misWeight = 1.f;
            if (kUseMIS && pdf > 0.f)
            {
                float brdfPdf = evalPdfScatter(sd, mtl, lightSample.dir);
                misWeight = evalMIS(params, numSamples, pdf, 1, brdfPdf);
            }

            // Compute offset light sample position to reduce self-intersections at the light.
            // We compute the shadow ray parameters based on the offset position.
            float3 offsetPos = computeRayOrigin(lightSample.posW, lightSample.normalW);
            float3 toLight = offsetPos - rayOrigin;
            ls.rayDistance = length(toLight);
            ls.rayDir = normalize(toLight);

            // Setup returned sample.
            ls.dir = lightSample.dir;
            ls.distance = lightSample.distance;
            ls.pdf = pdf;
            ls.Li = pdf > 0.f ? lightSample.Le * misWeight / (pdf * numSamples) : float3(0);

            return true;
        }
    }

    return false;
}

/** Generates a shadow ray for sampling the light sources.
    This should be called before generateScatterRay() as the latter updates the path throughput.
    The function assumes path.origin is the ray origin for the shadow ray, and that the PathData
    flags for the shadow ray have already been cleared.
    \param[in] params Path tracer parameters.
    \param[in] envMapSampler Environment map sampler.
    \param[in] emissiveSampler Emissive triangle sampler.
    \param[in] sd Shading data.
    \param[in] mtl The material at the shading point.
    \param[in] i The sample index in the range [0, kLightSamplesPerVertex).
    \param[in,out] path Path data. The path flags will be updated to enable the i:th shadow ray if a sample was generated.
    \param[in,out] sg Sample generator.
    \param[in,out] shadowRay Shadow ray parameters and unoccluded contribution for the generated sample.
    \return True if a sample was generated, false otherwise.
*/
bool generateShadowRayToEmissiveTriangle<S : ISampleGenerator, Material : IMaterial>(const PathTracerParams params, const EnvMapSampler envMapSampler, const EmissiveTriangleSampler emissiveSampler, const ShadingData sd, const Material mtl, const uint i, inout PathData path, inout S sg, inout ShadowRay shadowRay)
{
    // Sample the scene lights.
    SceneLightSample ls;
    bool valid = sampleEmissiveTriangle(params, envMapSampler, emissiveSampler, sd, mtl, path.origin, kLightSamplesPerVertex, i, sg, ls);

    if (valid && any(ls.Li > 0.f))
    {
        float3 Lr = mtl.eval(sd, ls.dir) * ls.Li * path.thp;
        if (any(Lr > 0.f))
        {
            // The sample is valid and has a non-zero contribution.
            // Store ray parameters and unoccluded radiance, weighted by path throughput.
            path.flags |= (uint(PathFlags::shadowRay) << i);
            shadowRay.rayParams = float4(ls.rayDir, ls.rayDistance);
            shadowRay.Lr = Lr;
            return true;
        }
    }
    return false;
}
