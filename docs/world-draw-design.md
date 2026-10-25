# World drawing without DX7: design

Status: draft for review. Follows step 5 of the generic backend work (ImGui through
`cIGZGDriver`) and extends the same idea to everything plugins draw in the city.

## Goal

Plugins that draw in the world today all reach for the DX7 device through
`AcquireD3DInterfaces`. That only works on the game's built-in DirectX driver. On SCD3D11 and
scvk those overlays simply disappear. This design gives plugins a world drawing API that goes
through the game's own draw context and graphics driver, so the same plugin code works on DX7,
SCD3D11 and scvk.

## Who needs what

Surveyed: Plop and Paint (`PaintOverlay`), Terrain Extensions (`OverlayRenderer`, slope and
contour renderers), the RS samples, the RS terrain decal service, and the upcoming Mac
submenus mod (as a preview of features Windows players will ask for).

| Client | What it draws | State | When |
|---|---|---|---|
| Plop and Paint | grids, shapes, markers, thick lines (crossed quads when vertical) | depth test on, no depth write, alpha blend, depth bias 8, no fog | end of PreDynamic |
| Terrain Extensions | overlays, slope cells, contour lines, labels | depth test off (always on top), alpha blend, depth bias 1, fog on | end of PostDynamic |
| Road decal sample | committed, selected and preview strips on the terrain | depth test LessEqual, no depth write, alpha blend, depth bias 1, fog on | end of PreDynamic |
| DrawService sample | world quad, screen lines, bounding boxes | depth test on, depth bias -16 to 16, screen-space lines | selectable |
| WorldProjection sample | ImGui labels and images placed with `WorldToScreen` | none, plus a DX7 depth readback for hiding labels | ImGui frame |
| Terrain decal service | clipped textured decals | game draw context, depth offset 2 to 4, texture transform | game decal pass |
| Mac mod (v3) | brush previews, grids, footprints, thumbnails at the cursor | ImGui 2D, no depth | after the UI |
| Mac mod (post FX) | colour grading, tilt shift, weather, night lights | shaders, scene copy, depth texture | at present |

What they share:

- Position + colour triangle lists, alpha blended, no culling, no lighting, no depth write.
  This covers almost every world draw.
- Depth test on or off, a small depth bias, optional fog so overlays fade like the scene.
- Thick lines built on the CPU as quads, terrain-height sampling per vertex, and layers that
  are rebuilt only when their content changes. Plop and Paint and Terrain Extensions each wrote
  their own version of all three.
- A clean camera. Today plugins draw with whatever matrices the game left behind. Terrain
  Extensions found that the model matrix can still hold the last object's transform and now
  resets the draw context and camera before every draw.
- Saving and restoring every state they touch (a copy of `D3D7StateGuard` in each repo).

## What the driver can and cannot do

`cIGZGDriver` is a fixed-function, OpenGL-1.x style interface. It covers matrices, blend,
depth, fog, polygon offset, stencil, texture stages and `DrawArrays`/`DrawElements`. It has no
render targets, no pixel or depth readback, no shaders and no blend equation (so no MAX blend).

The game's `SC4DrawContext` sits on top of the driver and keeps its own cache of the state it
has set. Calling the driver directly behind its back leaves that cache stale, which is why the
ImGui renderer has to resync it afterwards. World drawing should go through the draw context
instead, which keeps the cache correct by construction:

- `SC4DrawContext::DrawPrims` at `0x007D2990` takes `(prim, vertexFormat, count, vertices)`
  and is already used by the terrain decal renderer with format `0x0B`.
- Blend, depth test, depth mask, depth func, depth offset, cull, fog, lighting, texture and
  texture transform setters are already wrapped by `cIGZDrawService`.
- `SetDefaultRenderStateUnilaterally` and `SetCamera` give a known starting point.

Only things the draw context cannot express (if any turn up) fall back to direct driver calls,
followed by the same resync the ImGui renderer uses.

## Proposed API

A new interface on the existing draw service, `cIGZWorldDrawService`, with its own interface
ID and an API version so it can grow. Everything that builds geometry lives in a header-only
helper, so the binary interface stays small.

### Draw callbacks

```cpp
struct SC4WorldVertex {
    float x, y, z;     // world space, metres
    uint32_t color;    // 0xAARRGGBB
    float u, v;        // ignored when the batch has no texture
};

enum class SC4WorldBlend : uint8_t { Alpha, Additive, Multiply, Premultiplied, Opaque };

struct SC4WorldDrawState {
    SC4WorldBlend blend = SC4WorldBlend::Alpha;
    bool depthTest = true;      // false draws on top of everything
    bool depthWrite = false;
    int8_t depthBias = 1;       // same scale as the game's depth offset
    bool cullBackFaces = false;
    bool fog = false;           // fade with distance like the scene
    ImGuiTextureHandle texture{};  // zero id means untextured
};

using WorldDrawCallback = void (*)(cIGZWorldDrawContext& ctx, void* userData);

bool RegisterWorldDrawCallback(DrawServicePass pass, WorldDrawCallback cb, void* userData,
                               uint32_t* outToken);
void UnregisterWorldDrawCallback(uint32_t token);
```

RS runs these callbacks at the end of the chosen pass. Before the first callback it resets the
draw context, applies the active camera and an identity model transform. After the last one it
restores the default render state so the game carries on from a known state. Clients pick the
pass explicitly. There is no default, because the choice decides what the overlay hides behind
(end of PreDynamic: behind buildings but not cars, end of PostDynamic: behind everything).

`cIGZWorldDrawContext` is only valid during the callback:

```cpp
void DrawTriangles(const SC4WorldDrawState&, const SC4WorldVertex*, uint32_t count);
void DrawIndexedTriangles(const SC4WorldDrawState&, const SC4WorldVertex*, uint32_t vertexCount,
                          const uint16_t* indices, uint32_t indexCount);
void DrawLines(const SC4WorldDrawState&, const SC4WorldLinePoint*, uint32_t count,
               float width, SC4LineWidthUnit unit);  // Metres or Pixels
```

`DrawLines` expands to quads inside RS. With `Pixels` it uses the current camera to keep a
fixed screen width, which neither client can do today. Vertical segments get the crossed quad
treatment that Plop and Paint uses.

### Retained layers

Both big clients keep geometry that changes rarely and draw it every frame. RS can own that:

```cpp
uint32_t CreateWorldLayer(DrawServicePass pass, const SC4WorldDrawState& state, int32_t order);
bool SetWorldLayerTriangles(uint32_t layer, const SC4WorldVertex*, uint32_t count);
bool SetWorldLayerVisible(uint32_t layer, bool visible);
void DestroyWorldLayer(uint32_t layer);
```

Layers are drawn in `order` within their pass, before that pass's callbacks. This replaces
`PaintOverlay`'s layer list and `OverlayDrawManager` without either plugin keeping a renderer.

### Geometry helpers (header only)

`WorldGeometryBuilder.h` builds `SC4WorldVertex` arrays:

- `AddQuad`, `AddThickLine` (metres), `AddCircle`, `AddFilledPolygon`, `AddMarker`
- terrain draped versions that sample `cISTETerrain::GetAltitude` along the path, with a lift
  above the ground and optional subdivision so long lines follow hills
- colour helpers that turn RGBA floats into the packed vertex colour

These are the routines that Plop and Paint and Terrain Extensions each wrote separately.

### Textures

The ImGui service's managed textures become usable in world draws too, so one handle works
in `ImGui::Image` and in `SC4WorldDrawState::texture`. ImGui service API 6 adds:

- `UpdateTexture(handle, pixels)` through `LoadTextureLevel`. The WorldProjection sample and
  the Mac mod both replace a texture every frame today.
- `ImGuiTextureDesc` options for repeat wrapping and mipmaps, for ground fills and patterns.

### Picking

- `PickTerrain(screenX, screenY, float out[3])` using the camera eye ray and terrain altitude.
  The Mac mod rebuilds a 20 by 12 grid of game pick calls every frame to fake this.
- `GetTerrainHeight(x, z)` as a thin wrapper.
- `IsPointerOverImGui()` on the ImGui service. Detecting the game's own UI windows is left for
  later research.

## Terrain decals

The decal service is already mostly independent of the driver:

- The hooks sit in game code (`cSTEOverlayManager` call sites), not in the driver.
- `ClippedTerrainDecalRenderer` draws with `SC4DrawContext::DrawPrims`, `SetDepthOffset` and
  `SetTexTransform4`, all of which go through the active driver.
- The overlay manager sample only calls `cISTEOverlayManager`.

The one DX7 part is shadow recovery. After the game draws shadows, managed decals are drawn
again so shadows don't hide them completely. There are two ways this runs today:

1. **Composite** (DX7 only): draw all decals into an offscreen target, then blend that image
   over the scene once at 25 percent. Overlapping decals blend with each other first, so
   the result looks like one layer.
2. **Replay**: draw each decal again after the shadows at 25 percent of its own opacity.
   Overlapping decals stack, so overlaps come out slightly darker than in the composite.

When the D3DX object is missing, which is the case on SCD3D11 and scvk since step 4,
`CompositeManagedDecalsAfterShadows_` returns false and the replay runs instead. So decals
should already work on every driver, with the replay look for shadows.

### What the composite actually computes

The offscreen target is cleared to transparent black and the decals are drawn into it with
normal alpha blending. That image is then blended over the scene with its alpha scaled by
`k` (0.25). For a single decal with colour `C` and alpha `a` that gives
`S * (1 - k*a*a) + k * a*a*a * C`. Where decals are opaque (`a = 1`) the result is
`S * (1 - k) + k * C_top`, using only the topmost decal. The replay instead adds one
`k * a` layer per decal, which is why overlaps come out darker.

### Doing it through the game's own driver

The driver interface has no offscreen targets, but every driver also implements two extension
interfaces the game requires, and SCD3D11 has a real stencil buffer (`D24_UNORM_S8_UINT`, with
`StencilFunc`/`StencilOp`/`ClearStencil` implemented):

- `cIGZGSnapshotExtension` (`0xE69BFE2A`): `CopyColorBuffer` reads the colour buffer back to
  the CPU. The game uses it for snapshots and region thumbnails.
- `cIGZGBufferRegionExtension` (`0x669565FE`): saves part of the colour, depth or stencil
  buffer and copies it back later. The game uses it to avoid redrawing the whole scene. The
  copy back is a plain copy, it cannot blend.

Options, best first:

1. **Stencil, one recovery layer per pixel.** Clear stencil, then draw the replay from the
   topmost decal down, with the stencil test passing only where the value is still 0 and
   setting it to 1 on pass. An alpha test at a very low threshold keeps fully transparent
   texels from claiming pixels. Each pixel then gets exactly one recovery layer, from its
   topmost decal, which matches the composite for opaque decals and their overlaps. It runs
   entirely on the GPU and costs one extra stencil clear. The difference that remains is at
   soft, partly transparent decal edges, where the composite's `a*a*a` falloff is weaker
   than the replay's `a`. The replay can match that by drawing with `k * a*a*a` instead of
   `k * a`, computed per decal from its slot opacity. The texture's own alpha still enters
   once, so soft texture edges stay slightly stronger than the composite.
   Needs: the replay geometry buffered so it can be drawn in reverse order (the hook already
   computes it per decal), and stencil state set through the driver, then reset.
2. **Exact composite through a CPU round trip.** Draw the decals at full strength over the
   scene, read the result back with `CopyColorBuffer`, put the saved scene back with a buffer
   region, and draw the readback at 25 percent with `BitBltAlpha` or as a texture. This gives
   exactly the composite on every driver, but it reads a full frame back to the CPU each time
   decals are drawn. That stalls the GPU, so it is a fallback for screenshots at most.
3. **Plain replay** as today, when there is no stencil buffer.

If option 1 holds up on all three drivers it can replace the DX7 composite as well, which
removes the last DX7 dependency from the decal service and makes decals look the same
everywhere.

Plan for decals:

- Test the decal sample on SCD3D11 and scvk and confirm the replay path runs there today.
- Check that the game's depth buffer has stencil bits on the built-in DX7 driver and on scvk,
  and that the game does not rely on stencil contents across the decal pass (so clearing it
  there is safe).
- Prototype option 1 behind a setting and compare it side by side with the DX7 composite.
- Log once which shadow recovery method is in use, in plain words, so players know what to
  expect.

## Out of scope

These need render targets, readback or shaders. The fixed-function driver interface offers
none of them:

- Offscreen rendering and compositing, other than the decal composite on DX7.
- Reading back depth or colour. The WorldProjection depth readback only exists to hide labels
  behind the scene, which a depth-tested world draw now does directly. It stays as a DX7-only
  demo.
- Post processing like the Mac mod's colour grading, tilt shift, depth of field and weather.
  This would need an optional extension interface implemented by SCD3D11 and scvk themselves
  (scene colour copy, depth as a texture, offscreen targets, custom shaders). Worth proposing to
  their maintainers later, not something RS can do alone.
- Live 3D model previews and in-game model thumbnails. Both need either offscreen targets or
  drawing game models through the game's own code, which is separate research.
- MAX blending.

`AcquireD3DInterfaces` stays for these DX7-only features and returns false on other drivers,
as it does now.

## Migration

- **Road decal sample**: port first, as the reference client for the callback API.
- **DrawService sample**: port the world quad and screen lines. Keep the bounding box demo,
  which already uses the draw context.
- **WorldProjection sample**: add a depth-tested variant of the overlay through the new API.
  Keep the readback demo marked as DX7 only.
- **Terrain Extensions and Plop and Paint**: not changed by this work, since those repos are
  off limits. Their maintainers can move `OverlayRenderer` and `PaintOverlay` onto layers and the
  geometry helpers. This document and the samples are the migration guide.

## Verification

Each step is checked in game on the built-in DirectX driver, SCD3D11 and scvk:

- the road decal sample: strips sit on the terrain, hide behind hills, fade with fog
- a contour-style layer drawn on top of everything with depth test off
- pixel-width lines keep their width while zooming
- a textured ground quad using an ImGui texture handle
- terrain decals with shadow recovery, comparing composite (DX7) and replay
- nothing else in the scene changes: no stray transforms on buildings, no lost fog, no
  texture left bound on the UI

## Open checks before coding

- Which `SC4DrawContext` vertex format takes position, colour and UV. Format `0x0A` (24
  bytes, colour at 12, UV at 16) is what the ImGui renderer uses with the driver directly.
  Confirm the draw context accepts it, or use the decal format `0x0B`.
- That turning texturing off through the draw context really disables stage 0 on all three
  drivers, so untextured batches don't pick up the last bound texture.
- Whether fog through `SetFog` matches the scene's fog without RS supplying colour and range,
  or whether those have to be read from the game.
