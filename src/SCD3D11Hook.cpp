#include "SCD3D11Hook.h"

#include "cISGLDX7D3DX.h"
#include "utils/Logger.h"

namespace
{
    constexpr wchar_t kSCD3D11ModuleName[] = L"SCD3D11.dll";
    std::atomic<SCD3D11Hook::FrameCallback> g_frameCallback{};
    SCD3D11UnregisterFrameCallbackFn g_unregister{};
    bool g_registered{};

    void __stdcall ForwardFrame(SCD3D11FrameContext const* frame, void*) {
        auto const callback = g_frameCallback.load(std::memory_order_acquire);
        if (callback != nullptr) callback(frame);
    }
}

bool SCD3D11Hook::IsSCD3D11Driver(cIGZGDriver* driver) {
    return driver != nullptr
        && driver->GetGZCLSID() == kSCGDriverDirectX
        && GetModuleHandleW(kSCD3D11ModuleName) != nullptr;
}

bool SCD3D11Hook::CaptureInterface(cIGZGDriver* driver) {
    if (!IsSCD3D11Driver(driver)) return false;
    if (g_registered) return true;

    HMODULE const module = GetModuleHandleW(kSCD3D11ModuleName);
    auto const registerCallback = reinterpret_cast<SCD3D11RegisterFrameCallbackFn>(
        GetProcAddress(module, "SCD3D11RegisterFrameCallback"));
    g_unregister = reinterpret_cast<SCD3D11UnregisterFrameCallbackFn>(
        GetProcAddress(module, "SCD3D11UnregisterFrameCallback"));
    if (registerCallback == nullptr || g_unregister == nullptr || !registerCallback(ForwardFrame, nullptr)) {
        LOG_ERROR("SCD3D11Hook: SCD3D11 frame service API is unavailable or already owned");
        g_unregister = nullptr;
        return false;
    }
    g_registered = true;
    LOG_INFO("SCD3D11Hook: registered the SCD3D11 frame callback");
    return true;
}

void SCD3D11Hook::SetFrameCallback(FrameCallback callback) {
    g_frameCallback.store(callback, std::memory_order_release);
}

void SCD3D11Hook::Shutdown() {
    g_frameCallback.store(nullptr, std::memory_order_release);
    if (g_registered && g_unregister != nullptr) g_unregister(ForwardFrame, nullptr);
    g_unregister = nullptr;
    g_registered = false;
}

bool SCD3D11Hook::IsCaptured() {
    return g_registered;
}
