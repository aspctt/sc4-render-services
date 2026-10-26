#pragma once

#include <cstdint>

// Unique IDs for the ImGui service and its interface.
static constexpr auto kImGuiServiceID = 0xA4F2D0C1;

// The interface ID changes whenever the ImGui a plugin was built against stops matching the one
// SC4RenderServices.dll exports. A plugin built for another ImGui then cannot get the service,
// and runs without UI instead of calling into the wrong ImGui.
//   0x9B6F8E21  API 1-4: ImGui 1.92.1 in a separate imgui.dll (retired, no longer served)
//   0x40269664  API 5:   ImGui 1.93.0 docking, exported by SC4RenderServices.dll
static constexpr auto GZIID_cIGZImGuiService = 0x40269664;
static constexpr uint32_t kImGuiServiceApiVersion = 5;
