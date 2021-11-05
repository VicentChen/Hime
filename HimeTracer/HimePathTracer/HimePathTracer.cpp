/***************************************************************************
 # Copyright (c) 2015-21, NVIDIA CORPORATION. All rights reserved.
 #
 # Redistribution and use in source and binary forms, with or without
 # modification, are permitted provided that the following conditions
 # are met:
 #  * Redistributions of source code must retain the above copyright
 #    notice, this list of conditions and the following disclaimer.
 #  * Redistributions in binary form must reproduce the above copyright
 #    notice, this list of conditions and the following disclaimer in the
 #    documentation and/or other materials provided with the distribution.
 #  * Neither the name of NVIDIA CORPORATION nor the names of its
 #    contributors may be used to endorse or promote products derived
 #    from this software without specific prior written permission.
 #
 # THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS "AS IS" AND ANY
 # EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 # IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 # PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 # CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 # EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 # PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 # PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 # OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 # (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 # OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 **************************************************************************/
#include "HimePathTracer.h"
#include "RenderGraph/RenderPassHelpers.h"
#include "Scene/HitInfo.h"
#include <sstream>

using namespace Falcor;

namespace
{
    const char kShaderFile[] = "RenderPasses/Hime/HimeTracer/HimePathTracer/HimePathTracer.rt.slang";
    const char kParameterBlockName[] = "gData";

    // Ray tracing settings that affect the traversal stack size.
    // These should be set as small as possible.
    // The payload for the scatter rays is 8-12B.
    // The payload for the shadow rays is 4B.
    const uint32_t kMaxPayloadSizeBytes = HitInfo::kMaxPackedSizeInBytes;
    const uint32_t kMaxAttributeSizeBytes = 8;
    const uint32_t kMaxRecursionDepth = 1;

    // Render pass internal channels.
    const std::string kEmissiveTriangleInternal = "EmissiveTriangle";
    const std::string kEmissiveTriangleTexname = "gEmissiveTriangle";
    const std::string kPositionInternal = "Position";
    const std::string kPositionInternalTexname = "gInternalPosition";

    const Falcor::ChannelList kInternalChannels =
    {
        { kEmissiveTriangleInternal, kEmissiveTriangleTexname, "Emissive triangle index", true }, // [Hime]TODO: make this channel as a member, so we can update it's size.
        { kPositionInternal        , kPositionInternalTexname, "World space position"   , true },
    };

    // Render pass output channels.
    const std::string kColorOutput = "color";
    const std::string kAlbedoOutput = "albedo";
    const std::string kTimeOutput = "time";
    const std::string kScreenDebugOutput = "debug";

    const Falcor::ChannelList kOutputChannels =
    {
        { kColorOutput,       "gOutputColor",  "Output color (linear)",                           true /* optional */                           },
        { kAlbedoOutput,      "gOutputAlbedo", "Surface albedo (base color) or background color", true /* optional */                           },
        { kTimeOutput,        "gOutputTime",   "Per-pixel execution time",                        true /* optional */, ResourceFormat::R32Uint  },
    };

    const Falcor::ChannelList kExtraOutputChannels =
    {
        { kScreenDebugOutput, "gOutputDebug",  "Debug output",                                    true /* optional */                           },
    };
    // UI variables.
    // Note: To serialize these variables in render graph file, we need to modify 3 functions:
    //          * HimePathTracer::getScriptingDictionary
    //          * HimePathTracer::registerBindings
    //          * HimePathTracer::HimePathTracer
    // [Hime]TODO: Additional UI variables will lead to warnings in PathTracer.
    const char kUseGroundTruthShadowRay[] = "useGroundTruthShadowRay";
    const char kAccumulateShadowRay[] = "accumulateShadowRay";
    const char kUseReflectionRay[] = "useReflectionRay";
    const char kSampleWithProvidedUV[] = "sampleWithProvidedUV";
    const char kIgnoreShadowRayVisibility[] = "ignoreShadowRayVisibility";
}

const char* HimePathTracer::sDesc = "Hime path tracer";

HimePathTracer::SharedPtr HimePathTracer::create(RenderContext* pRenderContext, const Dictionary& dict)
{
    return SharedPtr(new HimePathTracer(dict));
}

Dictionary HimePathTracer::getScriptingDictionary()
{
    Dictionary d = PathTracer::getScriptingDictionary();
    d[kUseGroundTruthShadowRay] = mTracerParams.useGroundTruthShadowRay;
    d[kUseReflectionRay] = mTracerParams.useReflectionRay;
    d[kIgnoreShadowRayVisibility] = mTracerParams.ignoreShadowRayVisibility;
    d[kAccumulateShadowRay] = mTracerParams.accumulateShadowRay;
    d[kSampleWithProvidedUV] = mTracerParams.sampleWithProvidedUV;
    return d;
}

void HimePathTracer::registerBindings(pybind11::module& m)
{
    pybind11::class_<HimePathTracer, RenderPass, HimePathTracer::SharedPtr> pass(m, "HimePathTracer");
    pass.def_property(kUseGroundTruthShadowRay, &HimePathTracer::getUseGroundTruthShadowRay, &HimePathTracer::setUseGroundTruthShadowRay);
    pass.def_property(kUseReflectionRay, &HimePathTracer::getUseReflectionRay, &HimePathTracer::setUseReflectionRay);
    pass.def_property(kIgnoreShadowRayVisibility, &HimePathTracer::getIgnoreShadowRayVisibility, &HimePathTracer::setIgnoreShadowRayVisibility);
    pass.def_property(kAccumulateShadowRay, &HimePathTracer::getAccumulateShadowRay, &HimePathTracer::setAccumulateShadowRay);
    pass.def_property(kSampleWithProvidedUV, &HimePathTracer::getSampleWithProvidedUV, &HimePathTracer::setSampleWithProvidedUV);
}

HimePathTracer::HimePathTracer(const Dictionary& dict)
    : PathTracer(dict, kOutputChannels)
{
    for (const auto& [key, value] : dict)
    {
        if (key == kUseGroundTruthShadowRay) mTracerParams.useGroundTruthShadowRay = value;
        else if (key == kAccumulateShadowRay) mTracerParams.accumulateShadowRay = value;
        else if (key == kUseReflectionRay) mTracerParams.useReflectionRay = value;
        else if (key == kSampleWithProvidedUV) mTracerParams.sampleWithProvidedUV = value;
        else if (key == kIgnoreShadowRayVisibility) mTracerParams.ignoreShadowRayVisibility = value;
    }
}

RenderPassReflection HimePathTracer::reflect(const CompileData& compileData)
{
    auto reflector = PathTracer::reflect(compileData);

    for (const auto& internalChannel : kInternalChannels)
    {
        reflector.addInternal(internalChannel.name, internalChannel.desc)
            .format(ResourceFormat::RGBA32Float) // [Hime]TODO: We should use RGFloat here, because we only need triangle index and pdf. Currently we use extra 2 channels for debug.
            .bindFlags(ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess)
            .texture2D(mSharedParams.frameDim.x, mSharedParams.frameDim.y, 1, 1, mTracerParams.kMaxLightsPerPixel);
    }

    for (const auto& extraOutputChannel : kExtraOutputChannels)
    {
        reflector.addOutput(extraOutputChannel.name, extraOutputChannel.desc)
            .format(extraOutputChannel.format)
            .bindFlags(ResourceBindFlags::RenderTarget | ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess) // Notice that we bind all extra output channels as render target
            .texture2D(mSharedParams.frameDim.x, mSharedParams.frameDim.y);
    }

    return reflector;
}

void HimePathTracer::setScene(RenderContext* pRenderContext, const Scene::SharedPtr& pScene)
{
    PathTracer::setScene(pRenderContext, pScene);

    if (mpScene)
    {
        if (mpScene->hasGeometryType(Scene::GeometryType::Procedural))
        {
            logWarning("This render pass only supports triangles. Other types of geometry will be ignored.");
        }

        // Create ray tracing program.
        RtProgram::Desc desc;
        desc.addShaderLibrary(kShaderFile);
        desc.setMaxPayloadSize(kMaxPayloadSizeBytes);
        desc.setMaxAttributeSize(kMaxAttributeSizeBytes);
        desc.setMaxTraceRecursionDepth(kMaxRecursionDepth);
        desc.addDefines(mpScene->getSceneDefines());
        desc.addDefine("MAX_BOUNCES", std::to_string(mSharedParams.maxBounces));
        desc.addDefine("SAMPLES_PER_PIXEL", std::to_string(mSharedParams.samplesPerPixel));

        mTracer.pBindingTable = RtBindingTable::create(2, 2, mpScene->getGeometryCount());
        auto& sbt = mTracer.pBindingTable;
        sbt->setRayGen(desc.addRayGen("rayGen"));
        sbt->setMiss(kRayTypeScatter, desc.addMiss("scatterMiss"));
        sbt->setMiss(kRayTypeShadow, desc.addMiss("shadowMiss"));
        sbt->setHitGroupByType(kRayTypeScatter, mpScene, Scene::GeometryType::TriangleMesh, desc.addHitGroup("scatterClosestHit", "scatterAnyHit"));
        sbt->setHitGroupByType(kRayTypeShadow, mpScene, Scene::GeometryType::TriangleMesh, desc.addHitGroup("", "shadowAnyHit"));

        mTracer.pProgram = RtProgram::create(desc);
    }
}

void HimePathTracer::renderUI(Gui::Widgets& widget)
{
    auto group = widget.group("Hime Path Tracer Params", false);
    if (auto rayConfGroup = group.group("Ray Configurations", true))
    {
        group.checkbox("Use ground truth shadow ray", mTracerParams.useGroundTruthShadowRay, false);
        group.checkbox("Use reflection ray", mTracerParams.useReflectionRay, false);
        group.checkbox("Ignore shadow ray visibility", mTracerParams.ignoreShadowRayVisibility, false);
        group.checkbox("Accumulate ground truth shadow ray", mTracerParams.accumulateShadowRay, false);
        group.checkbox("Sample emissive triangle texture with provided UV", mTracerParams.sampleWithProvidedUV, false);
    }

    if (group.var("Num of emissive triangles per pixel", mTracerParams.lightsPerPixel, 1u, mTracerParams.kMaxLightsPerPixel, 1u))
    {
        // [Hime]TODO: Currently only support emissive triangles sampling.
        // Regenerate emissive texture if number of triangle samples changed.
        mTracerParams.isLightsPerPixelChanged = true;
    }

    PathTracer::renderUI(widget);
}

void HimePathTracer::execute(RenderContext* pRenderContext, const RenderData& renderData)
{
    // Call shared pre-render code.
    if (!beginFrame(pRenderContext, renderData)) return;

    // Clear internal and extra channels.
    clearTextures(pRenderContext, renderData);

    // Update emissive triangle texture if needed.
    if (!mTracerParams.useGroundTruthShadowRay)
    {
        updateEmissiveTriangleTexture(pRenderContext, renderData);
    }

    // Set compile-time constants.
    RtProgram::SharedPtr pProgram = mTracer.pProgram;
    setStaticParams(pProgram.get());

    // For optional I/O resources, set 'is_valid_<name>' defines to inform the program of which ones it can access.
    // TODO: This should be moved to a more general mechanism using Slang.
    pProgram->addDefines(getValidResourceDefines(mInputChannels, renderData));
    pProgram->addDefines(getValidResourceDefines(mOutputChannels, renderData));
    pProgram->addDefines(getValidResourceDefines(kInternalChannels, renderData));

    if (mUseEmissiveSampler)
    {
        // Specialize program for the current emissive light sampler options.
        assert(mpEmissiveSampler);
        if (pProgram->addDefines(mpEmissiveSampler->getDefines())) mTracer.pVars = nullptr;
    }

    // Prepare program vars. This may trigger shader compilation.
    // The program should have all necessary defines set at this point.
    if (!mTracer.pVars) prepareVars();
    assert(mTracer.pVars);

    // Set shared data into parameter block.
    setTracerData(renderData);

    // Bind I/O buffers. These needs to be done per-frame as the buffers may change anytime.
    auto bind = [&](const ChannelDesc& desc)
    {
        if (!desc.texname.empty())
        {
            auto var = mTracer.pVars->getRootVar();
            var[desc.texname] = renderData[desc.name]->asTexture();
        }
    };
    for (auto channel : mInputChannels) bind(channel);
    for (auto channel : mOutputChannels) bind(channel);
    for (auto channel : kInternalChannels) bind(channel);

    // Get dimensions of ray dispatch.
    const uint2 targetDim = renderData.getDefaultTextureDims();
    assert(targetDim.x > 0 && targetDim.y > 0);

    mpPixelDebug->prepareProgram(pProgram, mTracer.pVars->getRootVar());
    mpPixelStats->prepareProgram(pProgram, mTracer.pVars->getRootVar());

    // Spawn the rays.
    {
        PROFILE("HimePathTracer::execute()_RayTrace");
        mpScene->raytrace(pRenderContext, mTracer.pProgram.get(), mTracer.pVars, uint3(targetDim, 1));
    }

    // Update debug texture.
    if (mTracerParams.enableDebugTexture)
    {
        PROFILE("Update debug texture");
        updateDebugTexture(pRenderContext, renderData);
    }

    // Call shared post-render code.
    endFrame(pRenderContext, renderData);
}

Texture::SharedPtr HimePathTracer::getEmissiveTriangleTexture(const RenderData& renderData)
{
    Texture::SharedPtr pEmissiveTriangleTexture = renderData[kEmissiveTriangleInternal]->asTexture();
    return pEmissiveTriangleTexture;
}

void HimePathTracer::updateEmissiveTriangleTexture(RenderContext* pRenderContext, const RenderData& renderData)
{
    // No, we don't implement this function here.
    assert(false);
}

Texture::SharedPtr HimePathTracer::getDebugTexture(const RenderData& renderData)
{
    Texture::SharedPtr pDebugTexture = renderData[kScreenDebugOutput]->asTexture();
    return pDebugTexture;
}

void HimePathTracer::updateDebugTexture(RenderContext* pRenderContext, const RenderData& renderData)
{
    // No, we don't implement this function here.
}

Texture::SharedPtr HimePathTracer::getPositionTexture(const RenderData& renderData)
{
    Texture::SharedPtr pPositionTexture = renderData[kPositionInternal]->asTexture();
    return pPositionTexture;
}

void HimePathTracer::prepareVars()
{
    assert(mTracer.pProgram);

    // Configure program.
    mTracer.pProgram->addDefines(mpSampleGenerator->getDefines());

    // Create program variables for the current program.
    // This may trigger shader compilation. If it fails, throw an exception to abort rendering.
    mTracer.pVars = RtProgramVars::create(mTracer.pProgram, mTracer.pBindingTable);

    // Bind utility classes into shared data.
    auto var = mTracer.pVars->getRootVar();
    bool success = mpSampleGenerator->setShaderData(var);
    if (!success) throw std::exception("Failed to bind sample generator");

    // Create parameter block for shared data.
    ProgramReflection::SharedConstPtr pReflection = mTracer.pProgram->getReflector();
    ParameterBlockReflection::SharedConstPtr pBlockReflection = pReflection->getParameterBlock(kParameterBlockName);
    assert(pBlockReflection);
    mTracer.pParameterBlock = ParameterBlock::create(pBlockReflection);
    assert(mTracer.pParameterBlock);

    // Bind static resources to the parameter block here. No need to rebind them every frame if they don't change.
    // Bind the light probe if one is loaded.
    if (mpEnvMapSampler) mpEnvMapSampler->setShaderData(mTracer.pParameterBlock["envMapSampler"]);

    // Bind the parameter block to the global program variables.
    mTracer.pVars->setParameterBlock(kParameterBlockName, mTracer.pParameterBlock);
}

void HimePathTracer::setTracerData(const RenderData& renderData)
{
    auto pBlock = mTracer.pParameterBlock;
    assert(pBlock);

    // Upload parameters struct.
    pBlock["params"].setBlob(mSharedParams);

    // Bind emissive light sampler.
    if (mUseEmissiveSampler)
    {
        assert(mpEmissiveSampler);
        bool success = mpEmissiveSampler->setShaderData(pBlock["emissiveSampler"]);
        if (!success) throw std::exception("Failed to bind emissive light sampler");
    }
}

void HimePathTracer::setStaticParams(Program* pProgram) const
{
    PathTracer::setStaticParams(pProgram);

    // Set HimePathTracer static parameters.
    pProgram->addDefine("HIME_PATH_TRACER_PARAM", "1");
    pProgram->addDefine("USE_GROUND_TRUTH_SHADOW_RAY", mTracerParams.useGroundTruthShadowRay ? "1" : "0");
    pProgram->addDefine("IGNORE_SHADOW_RAY_VISIBILITY", mTracerParams.ignoreShadowRayVisibility ? "1" : "0");
    pProgram->addDefine("USE_REFLECTION_RAY", mTracerParams.useReflectionRay ? "1" : "0");
    pProgram->addDefine("ACCUMULATE_SHADOW_RAY_SAMPLES", mTracerParams.accumulateShadowRay ? "1" : "0");
    pProgram->addDefine("SAMPLE_WITH_PROVIDED_UV", mTracerParams.sampleWithProvidedUV ? "1" : "0");
}

void HimePathTracer::clearTextures(RenderContext* pRenderContext, const RenderData& renderData) const
{
    for (const auto& internalChannel : kInternalChannels)
    {
        Texture::SharedPtr pTexture = renderData[internalChannel.name]->asTexture();
        pRenderContext->clearTexture(pTexture.get());
    }

    for (const auto& extraOutputChannel : kExtraOutputChannels)
    {
        Texture::SharedPtr pTexture = renderData[extraOutputChannel.name]->asTexture();
        pRenderContext->clearTexture(pTexture.get());
    }
}
