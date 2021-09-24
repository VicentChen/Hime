#pragma once
#include "Falcor.h"

#ifdef BUILD_HIME_TRACER
#define himetracerdlldecl __declspec(dllexport)
#else
#define himetracerdlldecl __declspec(dllimport)
#endif

extern "C" __declspec(dllexport) void getPasses(Falcor::RenderPassLibrary& lib);

