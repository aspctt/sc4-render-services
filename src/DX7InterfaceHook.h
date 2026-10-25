#pragma once
#include <atomic>
#include "cIGZGDriver.h"
#include "cISGLDX7D3DX.h"

// Captures the native DirectX driver's D3DX object for the DX7 consumers that still need
// it (terrain decal shadow recovery, AcquireD3DInterfaces). ImGui no longer renders
// through DX7. It draws through cIGZGDriver.
class DX7InterfaceHook
{
public:
    static bool CaptureInterface(cIGZGDriver* pDriver);
    static void ReleaseInterface();
    static cISGLDX7D3DX* GetD3DXInterface();
    static std::atomic<cISGLDX7D3DX*> s_pD3DX;
};
