#pragma once

// Shared ABI contract exported by SCD3D11.dll. Native objects are borrowed and valid only
// for the duration of the callback.
#include <cstdint>
#include <d3d11.h>
#include <dxgi.h>
#include <Windows.h>

enum SCD3D11Event : uint32_t
{
    SCD3D11_EVENT_RENDER = 1,
    SCD3D11_EVENT_BEFORE_DEVICE_DESTROY = 2,
};

struct SCD3D11FrameContext
{
    uint32_t structSize;
    uint32_t apiVersion;
    SCD3D11Event event;
    uint32_t deviceGeneration;
    ID3D11Device* device;
    ID3D11DeviceContext* context;
    IDXGISwapChain* swapChain;
    ID3D11RenderTargetView* renderTargetView;
    HWND window;
};

using SCD3D11FrameCallback = void(__stdcall*)(SCD3D11FrameContext const*, void*);
using SCD3D11RegisterFrameCallbackFn = BOOL(__stdcall*)(SCD3D11FrameCallback, void*);
using SCD3D11UnregisterFrameCallbackFn = BOOL(__stdcall*)(SCD3D11FrameCallback, void*);
