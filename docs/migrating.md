# Updating a plugin to this version

This version of SC4RenderServices changes how plugins link ImGui, and adds a driver-independent
way to draw into the city. Every plugin that uses the ImGui service needs a rebuild. Plugins
that draw into the city with their own DirectX 7 or Direct3D 11 code can drop that code.

## What changed

- ImGui is the **docking** branch (1.93.0 docking) and is compiled into
  `SC4RenderServices.dll`. `imgui.dll` in the game's `Apps` folder is gone.
- ImGui draws through the game's `cIGZGDriver`, so it works with the built-in DirectX driver,
  SCD3D11 and scvk.
- The ImGui service has a new interface ID, `GZIID_cIGZImGuiService = 0x40269664` (API 5). The
  old ID `0x9B6F8E21` is no longer served, so an old plugin gets no ImGui service and runs
  without UI instead of calling into the wrong ImGui.
- New: `cIGZImGuiService::GetActiveDriverInfo` and the world draw service
  (`cIGZWorldDrawService`).

## Rebuilding against the new ImGui

1. Update the `sc4-render-services` submodule. ImGui's headers are now in
   `vendor/imgui` (the old `vendor/d3d7imgui/ImGui` folder is gone).
2. Stop linking `imgui.lib`. Link the ImGui that `SC4RenderServices.dll` exports instead.

If the plugin adds this repository with `add_subdirectory`, one call does everything:

```cmake
add_subdirectory(vendor/sc4-render-services EXCLUDE_FROM_ALL)
sc4rs_use_imgui(MyPlugin)
```

Configure the parent project for Win32 (`-A Win32`). If it adds its own `gzcom-dll`
(target `gzcom_dll`) or `spdlog` before this line, SC4RenderServices reuses those targets.

Without `add_subdirectory`, set up the same four things by hand:

- define `IMGUI_USER_CONFIG="ImGuiUserConfig.h"` and `IMGUI_DLL_IMPORT`, and put
  `vendor/imgui` and `src/public` on the include path
- link `SC4RenderServices.lib` from an SC4RenderServices build, and `delayimp.lib`
- link with `/DELAYLOAD:SC4RenderServices.dll`
- compile `src/public/SC4RenderServicesDelayLoad.cpp` into the plugin

The delay load matters. The game may load your plugin before `SC4RenderServices.dll`, and with
a normal import your plugin would then fail to load. With the delay load, ImGui resolves on the
first ImGui call. Only call ImGui once you hold the ImGui service, and it is always there by
then.

3. Docking is on. Windows can be docked into each other by the player, and the layout is
   saved to `LayoutFile` from `SC4RenderServices.ini`. Give windows stable, unique names
   (`"Title###MyPluginMainWindow"`) so their saved positions stay attached to them.

## Textures

`CreateTexture`, `GetTextureID` and `ReleaseTexture` work as before, but `GetTextureID` now
returns the driver's texture name, not an `IDirectDrawSurface7*`. Treat it as an opaque
`ImTextureID`. Code that cast it to a DirectDraw surface must stop doing that.

`ImGuiTextureDesc::useSystemMemory` is ignored.

## Finding out which driver is active

Do not look at the driver's class ID or check for `SCD3D11.dll` yourself: SCD3D11 reports the
built-in DirectX class ID, and scvk reports the OpenGL one. Ask the service:

```cpp
SC4GraphicsDriverInfo driver{};
if (imgui->GetActiveDriverInfo(driver) && driver.kind == SC4GraphicsDriverKind::BuiltInDirectX) {
    // Only here does AcquireD3DInterfaces succeed.
}
```

`AcquireD3DInterfaces` returns false with every driver except the built-in DirectX driver.

## Moving city drawing onto the world draw service

Plugins that drew into the city through raw DirectX, or through `cIGZDrawService` draw pass
hooks and the draw context, can use `cIGZWorldDrawService` instead. It works with every driver,
sets up and restores the game's render state itself, and splits large draws to fit the
built-in DirectX driver's vertex buffer. See [world-drawing.md](world-drawing.md) for the full
guide.

### Keep geometry in layers

The biggest change is where geometry lives. Most overlays only change when the player does
something, so build them once and keep them in a layer:

```cpp
uint32_t layer = world->CreateLayer(DrawServicePass::PreDynamic, style, 0);
// When the data changes:
world->SetLayerTriangles(layer, mesh.Data(), mesh.Count());
```

SC4RenderServices then draws the layer every frame with no work from your plugin. Rebuilding the
whole mesh and submitting it from a pass callback every frame is no longer needed. Use a draw
callback (`RegisterDrawCallback`) only for geometry that really changes every frame, such as a
preview following the mouse.

### Mapping the old render setup to a style

| Old code | `SC4WorldStyle` / call |
|---|---|
| draw at the end of the PreDynamic pass | `CreateLayer(DrawServicePass::PreDynamic, ...)` |
| draw at the end of the PostDynamic pass | `CreateLayer(DrawServicePass::PostDynamic, ...)` |
| depth test off | `.depth = SC4WorldDepth::AlwaysOnTop` |
| `SetDepthOffset(n)` / ZBIAS n | `.depthBias = n` |
| fog on with the scene's fog | `.fadeWithFog = true` |
| alpha blending | `.blend = SC4WorldBlend::Alpha` (the default) |
| `SetDefaultRenderStateUnilaterally`, `SetCamera`, restoring state afterwards | nothing, the service does it |
| raw DirectX textures | `.texture = handle` from `cIGZImGuiService::CreateTexture` |
| splitting draws to fit the built-in DirectX driver | nothing, the service does it |

Two examples from existing plugins:

- **Plop and Paint's paint overlay** drew at the end of PreDynamic with depth bias 8 and no fog:
  a `PreDynamic` layer with `SC4WorldStyle{.depthBias = 8}`, and a draw callback for the
  brush preview under the mouse.
- **Terrain Extensions' contour and slope overlays** drew at the end of PostDynamic with the
  depth test off, fog on and depth bias 1: a `PostDynamic` layer with
  `SC4WorldStyle{.depth = SC4WorldDepth::AlwaysOnTop, .fadeWithFog = true}`. Fill it when the
  overlay settings or the terrain change, not every frame.

### Helpers

`SC4WorldMeshBuilder` (header only) builds ground rectangles, outlines, circles, rings, polygons,
grids and lines, and drapes them over the terrain. `PickTerrain` turns ImGui's mouse position
into a point on the ground, and `GetTerrainHeight` gives the ground height at any point.

### What the world draw service cannot do

`cIGZGDriver` has no render-to-texture and no way to read back the frame or the depth buffer.
Post-processing, reading back depth, and rendering models into thumbnails still need
driver-specific code.
