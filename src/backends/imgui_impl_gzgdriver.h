// dear imgui: Renderer Backend for SimCity 4's cIGZGDriver
// This needs to be used along with a Platform Backend (e.g. Win32)
//
// Draws through the game's own graphics driver interface rather than a graphics API, so it
// works with whichever renderer plugin is active (the built-in DirectX 7 driver, SCD3D11,
// scvk, ...): every one of them implements cIGZGDriver because the game draws its own UI
// through it. Driver vocabulary and calling conventions are documented in
// docs/generic-backend-re.md.
//
// Implemented features:
//  [X] Renderer: User texture binding. Use the driver texture name as ImTextureID.
//  [X] Renderer: Large meshes support (64k+ vertices) even with 16-bit indices (ImGuiBackendFlags_RendererHasVtxOffset).
//  [X] Renderer: Texture updates support for dynamic font atlas (ImGuiBackendFlags_RendererHasTextures).
//  [X] Renderer: Expose selected render state for draw callbacks to use. Access in '(ImGui_ImplGZGDriver_RenderState*)GetPlatformIO().Renderer_RenderState'.
//  [ ] Renderer: Multi-viewport support. cIGZGDriver only draws into the game window.
//
// Call order each frame, at the game's present (just before cIGZGDriver::Flush):
//   ImGui_ImplWin32_NewFrame(); ImGui_ImplGZGDriver_NewFrame(driver); ImGui::NewFrame();
//   ... ImGui::Render(); ImGui_ImplGZGDriver_RenderDrawData(ImGui::GetDrawData());
// Everything here must run on the thread that renders, inside that window.

#pragma once
#include "imgui.h"
#include <cstdint>

class cIGZGDriver;

// [BETA] Selected render state data shared with callbacks.
// This is temporarily stored in GetPlatformIO().Renderer_RenderState during the ImGui_ImplGZGDriver_RenderDrawData() call.
// (Please open an issue if you feel you need access to more data)
struct ImGui_ImplGZGDriver_RenderState
{
    cIGZGDriver* Driver;
};

// Checks the driver can take the backend's vertex layout. Returns false (and leaves nothing
// initialized) if it cannot, in which case the backend must not be used with that driver.
bool ImGui_ImplGZGDriver_Init(cIGZGDriver* driver);
// Forgets every texture without calling the driver, which may already be gone at shutdown.
void ImGui_ImplGZGDriver_Shutdown();
// The driver can change across video mode changes; textures made with an earlier one are
// forgotten and recreated. Saves the game's texture bindings, so call this before anything
// in the frame can create a texture.
void ImGui_ImplGZGDriver_NewFrame(cIGZGDriver* driver);
void ImGui_ImplGZGDriver_RenderDrawData(ImDrawData* draw_data);

// Textures managed outside ImGui's own texture list. Pixels are RGBA32, tightly packed.
// Call these only between ImGui_ImplGZGDriver_NewFrame() and ImGui_ImplGZGDriver_RenderDrawData().
uint32_t ImGui_ImplGZGDriver_CreateTexture(const void* rgba_pixels, int width, int height);
void ImGui_ImplGZGDriver_DestroyTexture(uint32_t texture);
bool ImGui_ImplGZGDriver_IsTextureAlive(uint32_t texture);
