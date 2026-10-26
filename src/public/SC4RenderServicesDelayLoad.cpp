// Delay-load failure hook for plugins that use the ImGui exported by SC4RenderServices.dll.
//
// Add this file to a plugin that links SC4RenderServices.lib with
// /DELAYLOAD:SC4RenderServices.dll and delayimp.lib (sc4rs_use_imgui() in CMakeLists.txt does
// all three). Delay loading makes the plugin independent of load order: the game may load it
// before SC4RenderServices.dll, and the import only resolves on the first ImGui call.
//
// A plugin makes that call only after it obtained the ImGui service, so SC4RenderServices.dll
// is loaded by then and the loader hands back that module. If it is not (the DLL is missing, or
// an older one lacks a function), there is nothing safe to bind to: no stand-in can replace
// arbitrary ImGui functions. This hook writes the reason to the debugger output so the failure
// can be diagnosed, and the delay-load helper then raises its usual exception.

#include <Windows.h>
#include <delayimp.h>

#include <cstdio>

namespace {
    FARPROC WINAPI SC4RenderServicesDelayLoadFailure(const unsigned notify, const PDelayLoadInfo info) {
        char message[512];
        if (notify == dliFailLoadLib) {
            std::snprintf(message, sizeof(message),
                          "SC4RenderServices delay load: cannot load %s (error %lu). Is SC4RenderServices.dll "
                          "installed, and was the ImGui service obtained before calling ImGui?\n",
                          info->szDll, info->dwLastError);
        }
        else if (notify == dliFailGetProc) {
            if (info->dlp.fImportByName) {
                std::snprintf(message, sizeof(message),
                              "SC4RenderServices delay load: %s has no %s (error %lu). The installed "
                              "SC4RenderServices.dll is older than the one this plugin was built against.\n",
                              info->szDll, info->dlp.szProcName, info->dwLastError);
            }
            else {
                std::snprintf(message, sizeof(message),
                              "SC4RenderServices delay load: %s has no ordinal %lu (error %lu).\n",
                              info->szDll, info->dlp.dwOrdinal, info->dwLastError);
            }
        }
        else {
            return nullptr;
        }
        OutputDebugStringA(message);
        return nullptr;
    }
}

extern "C" const PfnDliHook __pfnDliFailureHook2 = SC4RenderServicesDelayLoadFailure;
