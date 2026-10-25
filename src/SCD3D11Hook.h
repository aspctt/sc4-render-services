#pragma once

#include <atomic>

#include "SCD3D11Service.h"
#include "cIGZGDriver.h"

class SCD3D11Hook
{
public:
    using FrameCallback = void (*)(SCD3D11FrameContext const* frame);

    static bool IsSCD3D11Driver(cIGZGDriver* driver);
    static bool CaptureInterface(cIGZGDriver* driver);
    static void SetFrameCallback(FrameCallback callback);
    static void Shutdown();
    static bool IsCaptured();
};
