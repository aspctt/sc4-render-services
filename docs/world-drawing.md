# Drawing in the city

`cIGZWorldDrawService` draws shapes, lines and textures into the 3D city view. It draws
through the game's own graphics driver, so the same plugin code works with the game's
built-in DirectX driver, SCD3D11 and scvk. Your plugin never touches DirectX.

Headers, all in `src/public`:

- `cIGZWorldDrawService.h`: the service, the vertex and style types, colour helpers.
- `SC4WorldMeshBuilder.h`: a header-only helper that builds ground shapes, lines, circles,
  polygons and grids, draped over the terrain.

## Getting the service

```cpp
#include "public/cIGZWorldDrawService.h"
#include "public/SC4WorldMeshBuilder.h"

cIGZWorldDrawService* world = nullptr;
if (!mpFrameWork->GetSystemService(kWorldDrawServiceID, GZIID_cIGZWorldDrawService,
                                   reinterpret_cast<void**>(&world))) {
    // An older SC4RenderServices.dll. Carry on without drawing.
}
// ...
world->Release();  // when your plugin shuts down
```

Call everything from the game's main thread, for example from an ImGui panel, a message
handler or an input control.

## Layers: draw once, shown every frame

A layer keeps its triangles inside SC4RenderServices and is drawn every frame without any
work from your plugin. Fill it when your data changes, not every frame. This fits almost
everything: grids, footprints, area overlays, road markings.

```cpp
uint32_t layer = world->CreateLayer(DrawServicePass::PreDynamic, SC4WorldStyle{}, 0);

SC4WorldMeshBuilder mesh;
mesh.DrapeOnTerrain(world);
mesh.AddGroundRectOutline({x, z}, 16.0f, 16.0f, 0.0f, 0.5f, SC4WorldColor(1, 1, 0));
mesh.AddGroundCircle({x, z}, 6.0f, SC4WorldColor(0, 0.5f, 1, 0.4f));
world->SetLayerTriangles(layer, mesh.Data(), mesh.Count());

world->SetLayerVisible(layer, false);  // hide it
world->DestroyLayer(layer);            // when you are done
```

`AddLayerTriangles` and `AddLayerLine` append to a layer, `ClearLayer` empties it.

## Draw callbacks: for things that change every frame

A callback runs at the end of a pass every frame, for things like a preview that follows
the mouse. Everything drawn here is thrown away after the frame.

```cpp
void DrawPreview(cIGZWorldDrawContext& context, void* userData) {
    auto* self = static_cast<MyTool*>(userData);
    context.DrawTriangles(SC4WorldStyle{.blend = SC4WorldBlend::Additive},
                          self->preview.Data(), self->preview.Count());
}

uint32_t token = 0;
world->RegisterDrawCallback(DrawServicePass::PostDynamic, &DrawPreview, this, &token);
// ...
world->UnregisterDrawCallback(token);
```

The context also offers `DrawIndexedTriangles`, `DrawLine`, `WorldToScreen`, and
`MetersPerPixel`. The last one lets you size a marker so it looks the same at every zoom level.

## Passes

The pass decides what can hide your drawing:

| Pass | Drawn after | Use for |
|---|---|---|
| `PreDynamic` | terrain, buildings, props | things on the ground: hidden by hills and buildings, cars drive over them |
| `PostDynamic` | everything in the scene | things that sit on top of everything else |

The other passes work too, but rarely make sense.

## Styles

Every field of `SC4WorldStyle` has a sensible default, so set only what you need:

| Field | Default | Meaning |
|---|---|---|
| `blend` | `Alpha` | `Additive` for glows, `Multiply` for shading, `Premultiplied`, `Opaque` |
| `depth` | `HiddenBehindScene` | `AlwaysOnTop` draws over everything |
| `depthBias` | `1` | pulls ground shapes forward so they don't flicker against the terrain |
| `writeDepth` | `false` | only for solid shapes that should hide later scenery |
| `fadeWithFog` | `false` | fades into the distance like the scene, when the player has fog on |
| `cullBackFaces` | `false` | only for closed solid shapes |
| `texture` | none | an `ImGuiTextureHandle` from the ImGui service, multiplied by the vertex colour |

## Lines

`SC4WorldLineStyle` sets the width and whether the line closes into a loop:

- `SC4WorldLineWidth::Pixels` (default): the line keeps its width on screen at every zoom
  level and always faces the camera. Good for routes, outlines and selections.
- `SC4WorldLineWidth::Meters`: the line has a fixed width in the world and lies flat, like
  a road marking.

Pixel lines in a layer are rebuilt from the camera every frame inside SC4RenderServices.

## Terrain

- `PickTerrain(screenX, screenY, x, y, z)` finds the ground under a screen point. ImGui's
  mouse position works directly.
- `GetTerrainHeight(x, z, height)` gives the ground height at a point.
- `SC4WorldMeshBuilder::DrapeOnTerrain` makes every ground shape follow the terrain. Long
  edges are cut into pieces of at most 4 m (`SetMaxEdgeLength`) and each corner is lifted
  5 cm above the ground.

## Textures

Create the texture with the ImGui service, then put its handle in the style. Textures are
lost when the graphics device resets, for example on Alt+Tab with the built-in driver.
Make a new one and give the layer the new handle with `SetLayerStyle`. Until then the
shapes using the old texture are skipped, and the log says why.

## Coordinates and colours

- World coordinates are in metres: x and z along the ground, y up. A tile is 16 m, and
  `SC4WorldMeshBuilder::AddGroundTile` draws one.
- Colours are `0xAARRGGBB`. `SC4WorldColor(r, g, b, a)` takes 0 to 1 floats.

## Examples

- `src/sample/WorldDrawSampleDirector.cpp` covers every feature: layers, both line types, a
  texture on the ground, a callback that follows the mouse, and terrain picking.
- `src/sample/road-decal/` is a real tool that keeps its markings in five layers.
