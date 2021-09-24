#include "HimeTracer.h"
#include "HimePathTracer/HimePathTracer.h"

// Don't remove this. it's required for hot-reload to function properly
extern "C" __declspec(dllexport) const char* getProjDir()
{
    return PROJECT_DIR;
}

extern "C" __declspec(dllexport) void getPasses(Falcor::RenderPassLibrary& lib)
{
    lib.registerClass("HimePathTracer", Falcor::HimePathTracer::sDesc, Falcor::HimePathTracer::create);

    Falcor::ScriptBindings::registerBinding(Falcor::HimePathTracer::registerBindings);
}
