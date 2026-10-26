#include "DX7InterfaceHook.h"

#include <atomic>
#include <d3d.h>

#include "utils/Logger.h"

bool DX7InterfaceHook::CaptureInterface(cIGZGDriver* pDriver)
{
    if (!pDriver) {
        LOG_ERROR("DX7InterfaceHook::CaptureInterface: null driver");
        return false;
    }

    const uint32_t driverClsid = pDriver->GetGZCLSID();

    if (driverClsid != kSCGDriverDirectX) {
        LOG_INFO("DX7InterfaceHook::CaptureInterface: unsupported driver clsid=0x{:08X}", driverClsid);
        return false;
    }

    // Offset 0x24C for this build (verified in logs)
    const auto driverPtr = static_cast<void*>(pDriver);
    cISGLDX7D3DX* candidate = *reinterpret_cast<cISGLDX7D3DX**>(
        static_cast<uint8_t*>(driverPtr) + 0x24C);

    if (!candidate) {
        return false;
    }
    if (!candidate->GetD3DDevice() || !candidate->GetDD()) {
        return false;
    }

    auto* current = s_pD3DX.load(std::memory_order_acquire);
    if (candidate != current) {
        if (candidate) {
            candidate->AddRef();
        }
        current = s_pD3DX.exchange(candidate, std::memory_order_acq_rel);
        if (current) {
            current->Release();
        }
    }
    return candidate != nullptr;
}

void DX7InterfaceHook::ReleaseInterface()
{
    if (auto* d3dx = s_pD3DX.exchange(nullptr, std::memory_order_acq_rel)) {
        d3dx->Release();
    }
}

cISGLDX7D3DX* DX7InterfaceHook::GetD3DXInterface()
{
    return s_pD3DX.load(std::memory_order_acquire);
}

// Static member definitions
std::atomic<cISGLDX7D3DX*> DX7InterfaceHook::s_pD3DX{nullptr};
