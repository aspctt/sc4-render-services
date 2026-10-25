# Generic backend: reverse-engineering notes

Findings for step 1 of `generic-backend-plan.md`. Addresses are image-base VAs in
`SimCity 4.exe` (Windows, base `0x00400000`) unless marked Mac (`x86-32-cpu0x3`, the
symbolized reference). Tools: Ghidra 12.2 with GhidraMCP 7.0.0, through `ghx` and three
small Ghidra scripts (`GhxScanVCalls`, `GhxFuncsWith`, `GhxDecompile`) kept beside `ghx`.

Confidence: **verified** = read in the Windows binary; **matched** = Windows function
paired with a Mac symbol by structure; **inferred** = still needs a runtime or
binary check.

## `cIGZGDriver` vtable

| Slot | Offset | Method | Notes |
| ---- | ------ | ------ | ----- |
| 12 | `0x3C` | `ColorMask(bool)` | |
| 13 | `0x40` | `DepthFunc` | |
| 14 | `0x44` | `DepthMask(bool)` | |
| 18 | `0x54` | `BlendFunc(src, dst)` | |
| 19 | `0x58` | `AlphaFunc(func, ref)` | |
| 28 | `0x70` | `TexEnv(target, pname, int)` | MSVC reverses the `TexEnv` pair; `0x6C` is the `float const*` form |
| 32 | `0x80` | `ColorMultiplier(r, g, b)` | |
| 33 | `0x84` | `AlphaMultiplier(a)` | |
| 34 | `0x88` | `EnableVertexColors(bool, bool)` | |
| 40 | `0xA0` | `MatrixMode` | 0 = model-view, 1 = projection |
| 41 | `0xA4` | `LoadMatrix(float const*)` | |
| 42 | `0xA8` | `LoadIdentity` | |
| **43** | **`0xAC`** | **`Flush`** | **verified** on the native driver |
| 44 | `0xB0` | `Enable(cap)` | |
| 45 | `0xB4` | `Disable(cap)` | |
| 51 | `0xCC` | `TexStage(stage)` | |
| 52 | `0xD0` | `TexStageCoord` | |
| 53 | `0xD4` | `TexStageMatrix` | |
| 67 | `0x10C` | `PolygonOffset` | |
| 74 | `0x128` | `SetViewport(x, y, w, h)` | MSVC order; on Mac `0x128` is `SetViewport()` |
| 75 | `0x12C` | `SetViewport()` | full-window reset |
| 76 | `0x130` | `GetViewport(int[4])` | left, top, right, bottom |
| 78 | `0x138` | `GetGZCLSID` | |
| 81 | `0x144` | `IsDeviceReady` | |

Capabilities for `Enable`/`Disable`: 0 AlphaTest, 1 DepthTest, 2 StencilTest, 3 CullFace,
4 Blend, 5 Texture2D (per texture stage), 6 Fog.

Native DirectX driver (**verified**):
- vtable `0x00AC4660` (labelled `cGDriver_vftable`). Slot 0 is `QueryInterface`.
- `cGDriver::GetGZCLSID` `0x00886E00`: `mov eax, 0xBADB6906; ret`. Its vtable entry at
  `0x00AC4798` fixes the base.
- `cGDriver::Flush` `0x00884BA0` (slot 43). It starts with
  `IDirectDraw7::TestCooperativeLevel` on `this+0x210` and handles device loss, which
  involves the D3DX object at `this+0x24C` that `GraphicsInterfaceHook` already uses. I
  read only the device-loss branch; the present itself is **inferred** from the
  contract inventory.

Correction to the plan: counting `virtual` lines in SCD3D11's annotated
`cIGZGDriver.h` also counts two comment lines that contain the word. Slots from
`SetViewport` onwards are two lower than a naive count. The header's own `// 0x...`
annotations are correct.

## Q1: the per-frame `Flush` call site

**Hook here:** `0x0098E769` in `cGZGraphicSystem::Draw` (`0x0098E5C0`, **matched** to Mac
`0x0043237C`).

```
0098e759  8b 55 00           mov  edx, [ebp]
0098e75c  8b cd              mov  ecx, ebp
0098e75e  ff 52 0c           call [edx+0x0c]          ; graphic system -> cIGZGDriver*
0098e761  3b c3              cmp  eax, ebx            ; ebx == 0
0098e763  74 0a              jz   0098e76f
0098e765  8b 10              mov  edx, [eax]
0098e767  8b c8              mov  ecx, eax
0098e769  ff 92 ac 00 00 00  call [edx+0xac]          ; driver->Flush()   <-- hook
0098e76f  88 5e 26           mov  [esi+0x26], bl      ; clear "needs present"
```

- How to hook it: overwrite the 6 bytes at `0x0098E769` with `call rel32; nop`, after
  checking the 13 bytes from `0x0098E765` (`8B 10 8B C8 FF 92 AC 00 00 00 88 5E 26`). The
  stub is `__fastcall(cIGZGDriver* driver)`. It draws ImGui, restores state, then calls
  `driver->Flush()` itself. `EBX`, `ESI`, `EDI` and `EBP` must survive, which is the
  normal MSVC callee-saved contract. `EAX` is not used afterwards.
- Why this is the frame boundary (Mac structure, which the Windows function repeats step
  for step):
  1. paint the root window tree (`[this+0xEC]->vfunc 0x3C`);
  2. if `this+0x25` is set, blit through the `0x8010` draw context;
  3. if `this+0x26` ("needs present") is set, blit the back buffer, get the driver,
     `Flush()`, clear the flag.

  The game's UI is therefore complete when the hook runs, so ImGui lands on top of it.
- `Draw` is referenced from two vtables (`0x00ADAA9C`, `0x00ADB094`), which are two
  graphic-system variants sharing the code. The `Draw` slot sits in a secondary-base
  vtable that starts at `0x00ADA9F8` (slot 41). The Windows object is reached through
  `this+4`, so member offsets are shifted against Mac (`0x41/0x42/0x64` →
  `0x25/0x26/0x48`).
- **Inferred, check at runtime:** `Flush` only runs when `this+0x26` is set, so ImGui
  redraws only when the game presents. If a static screen (paused, no animation) stops
  setting the flag, ImGui freezes. Log the hook rate on a paused city. If it drops, mark
  the graphic system dirty while ImGui wants input or is animating.

This is the present for every ordinary screen: main menu, region view and city view. The
region screen is a normal GZ window tree (Mac `cSC4WinRegion3DView::GZPaint`,
`cSC4WinRegionScroll::GZPaint`) painted in step 1 above, so ImGui draws over the region
view too. **Inferred** from the Mac structure; confirm on the first in-game test.

Other `Flush` callers. Every virtual call through `+0xAC` was scanned: 232 Windows sites,
226 Mac, mostly other interfaces with a method at that offset. Virtual calls cannot be
enumerated completely, so this list is not exhaustive:
- `cSC4ProgressScreen::Repaint` `0x007773B0`, call at `0x0077762B`: the loading-screen
  present, outside the frame loop. Not hooked, so ImGui does not draw on loading screens.
  It couldn't take input there anyway. If an overlay is ever wanted, this is the same
  6-byte `call [edx+0xac]` shape with the driver in `ECX`, so the main stub can be reused.
  A possible later addition, not planned.
- `cSC4WinRegionView::RenderTile` `0x007B4150`, call at `0x007B4493`: renders a region
  tile. Leaving it unhooked keeps ImGui out of region thumbnails.
- `cSC4WinSnapshotTaker` (three `0xAC` calls on Mac) and the region thumbnail path
  render outside `cGZGraphicSystem::Draw`. Hooking only `0x0098E769` keeps ImGui out of
  them. **Inferred**: verify with an in-game photo and a region thumbnail (step 7).

## Q2: the `SC4DrawContext` state cache

`SC4DrawContext::SetRenderState` (`0x007D4530`) diffs a 32-bit flag word (requested
`param[0]`, mask `param[1]`) against the cache at `this+0x80`. It issues only changed
states, all on stage 0 unless noted:

| Bits | State |
| ---- | ----- |
| `0x1` | `ColorMask` |
| `0x2` | `DepthMask` |
| `0x4` | DepthTest |
| `0x8` | AlphaTest (forced off by a global option) |
| `0x10` | Blend (forced off by a global option) |
| `0x20` | Texture2D, stage 0 |
| `0x40` | CullFace |
| `0x380` | `DepthFunc` |
| `0x1E01C00` | `AlphaFunc` and its reference |
| `0x1FE000` | `BlendFunc` src/dst |
| `0xE000000` | stage-0 `TexEnv` mode |
| `0xF0000000` | stage-1 Texture2D and `TexEnv` mode (multitexture only) |

Every call then re-applies `SetTexWrapModes` (`0x007D41E0`), `SetTexFiltering`
(`0x007D4280`) and `ClearTexTransform` (`0x007D4420`) for stages 0 and 1.

`SetDefaultRenderStateUnilaterally` (`0x007D5230`):
1. For every cached texture stage (vector of 0x68-byte `cStageInfo` at `this+0xE0..0xE4`),
   issues `TexStage(i)`, `TexStageMatrix(identity)`, `TexStageCoord(0)`.
2. Resets each `cStageInfo` to a default-constructed one.
3. Sets the cache to `~default` so every bit counts as changed, then calls
   `SetRenderState(&default)` with the default at `this+0x88`.

`SetDefaultRenderState` (`0x007D5200`) applies the default, or the top of a state stack
(`this+0x1EC/0x1F0`), through the normal diff.

The cache does **not** cover fog (or its parameters), stencil, the projection and
model-view matrices, the viewport, bound textures, color/alpha multipliers,
`EnableVertexColors`, combiners, shade model or polygon offset.

Reaching the draw context at `Flush` time: View3DWin → `GetRenderer()` →
`cSC43DRender::GetDrawContext` (`0x004E82A0`, `[this+0x11C]`), as
`DrawService::WrapRendererDrawContextInternal` already does. This exists only in city
view; region view and the menus have no View3DWin and no `SC4DrawContext`.

Per frame (**verified** for city view): `cSC43DRender::Draw` (`0x007CB530`) calls
`SetDefaultRenderStateUnilaterally` on its draw context at `0x007CB5D7`, before the draw
passes. Only a helper call just before it is conditional; I did not check for early
returns earlier in the function. The camera matrices come from
`SC4DrawContext::SetCamera` (Mac); that this runs every frame is **inferred**.

## Q3: the game's 2D UI projection

`cGZGraphicSystem::SetUIRenderingMode` (Mac `0x00431F68`) is the UI baseline. Every call
is an explicit setter:

- `IsDeviceReady()` or return.
- `TexStage(1)`, `Disable(Texture2D)`, `TexStage(0)`, `Enable(Texture2D)`,
  `TexStageMatrix(identity)`, `TexStageCoord(0)`.
- Disable Stencil, DepthTest (after a redundant enable), AlphaTest, Blend, CullFace and
  Fog.
- `AlphaMultiplier(1)`, `ColorMultiplier(1, 1, 1)`, `DepthMask(false)`, `ColorMask(true)`,
  `EnableVertexColors(true, true)`, `PolygonOffset(0)`.
- `SetViewport()` (full window), then `GetViewport(l, t, r, b)`.
- `MatrixMode(1)`, `LoadMatrix(orthoMat)`, `MatrixMode(0)`, `LoadIdentity()`.

`orthoMat` (Mac `0x00ABB7E0`), row-vector layout with the translation in elements 12–14:

```
[ 2/(r-l)   0         0    0 ]
[ 0        -2/(b-t)   0    0 ]
[ 0         0         0    0 ]
[ -1        1         0.5  1 ]
```

That is a top-left-origin pixel ortho with depth fixed at 0.5 and no half-pixel offset.
The game fills in the two scale terms only once, behind a static guard, so they keep the
first viewport size. ImGui should build the same matrix from `DisplayPos`/`DisplaySize`
every frame instead of copying that quirk.

`SetViewport(x, y, w, h)` takes a bottom-left Y. The game converts it as
`y = screenHeight − rect.bottom` (`0x00991C00`, which reads screen height from
`this+0x20`).

Not found yet: the Windows `SetUIRenderingMode`. Vtable distance doesn't transfer because
Windows puts `Draw` in a secondary-base vtable. It isn't needed for the backend: the
values above are what matters, and ImGui sets them itself.

## Decision: state hygiene

**Strategy A, extended.** No driver vtable shadowing. Agreed with the maintainer on
2026-10-25, together with the hook point.

At the hook, ImGui runs after the game's UI pass and directly before the present. The
next thing that reads state is the next frame. In city view that frame starts with
`SetDefaultRenderStateUnilaterally` and a camera load; the UI pass establishes its own
baseline with explicit setters. The two things that can go wrong are:

1. **The game's cache is stale.** ImGui changes cached states behind the cache, and a
   later `SetRenderState` diff skips a needed call. Fix: when an `SC4DrawContext`
   exists, call `SetDefaultRenderStateUnilaterally` after ImGui draws. The 3D pass does
   this again at the start of the next frame anyway, so the extra call costs nothing and
   also covers anything that draws through the context before that.
2. **Uncached state is left as ImGui set it.** Fix: after ImGui, replay the UI baseline
   above (all setters, nothing read back): Texture2D on stage 0 / off on stage 1,
   stencil/depth/alpha/blend/cull/fog off, multipliers 1, `DepthMask(false)`,
   `ColorMask(true)`, vertex colors on, `PolygonOffset(0)`, `SetViewport()`, the UI ortho
   projection and an identity model-view. This is the state the game itself leaves
   before its UI draws, so restoring it matches what the game expects at that point.

Strategy B (shadow every driver setter) isn't needed. Get* can't read state back, and
the game already re-establishes everything it depends on at a known point every frame.

Open items:
- `cStageInfo` (0x68 bytes) caches per-stage texture state, likely including the bound
  texture and combiner. `Unilaterally` resets it, so a later bind of the same texture
  should be re-issued. Confirm in the setter when step 5 needs it.
- Fog parameters and stencil func/op are not in the baseline because the UI only turns
  them off. ImGui never enables them, so nothing to restore.
- The dirty-flag cadence and the snapshot/thumbnail exclusion above need checking in
  game.

## Step 5: driver details the backend relies on

Native DirectX driver (all **verified** in the Windows binary, renamed in Ghidra):
- `cGDriver::SetTexture(texture, stage)` `0x00884810` (`+0xE8`) and `GetTexture(stage)`
  `0x00884860` (`+0xEC`) read and write the same per-stage field
  (`[this+0x11C] + stage*0x80 + 4`). SCD3D11 behaves the same way. The backend saves the
  game's stage 0/1 bindings with `GetTexture` and restores them with `SetTexture`.
- `cGDriver::TexImage2D` `0x00884880` (`+0x64`) creates the surface through
  `nSGLDX7::cTextureManager::CreateTexure`, then always loads level 0 from the pixel pointer
  without a null check. **Never pass null pixels.** Its row length comes from `PixelStore`
  (`this+0x134`).
- `cGDriver::LoadTextureLevel(texture, level, x, y, w, h, format, type, rowLength, pixels)`
  `0x00883040` (`+0xF4`) uses the same loader; `rowLength` 0 means `w`.

SCD3D11 (source): texture target 0; internal format 4 = RGBA8 (stored B8G8R8A8); upload
format 3 = BGRA with type 1 = unsigned byte is its no-conversion path (any other combination
is converted per texel through `double`); `TexParameter` changes the active stage's sampler
(0 mag, 1 min, 2 wrap U, 3 wrap V; filter 1 linear, wrap 2 clamp); `TexEnv(0, 0, 1)` is
modulate; primitive 0 = triangles; index type 3 = `uint16`; blend 4/5 = SrcAlpha/InvSrcAlpha;
`InterleavedArrays` stride 0 = packed. Format 10 is position@0, BGRA@12, UV@16, stride 24;
the backend checks this through `VertexFormatStride`/`VertexFormatElementOffset` at init.

Device loss: SCD3D11 recreates textures after device removal with their names and sizes but
**blank contents**, so `IsTexture` stays true and the backend's per-frame `IsTexture` check
cannot see it. There is no driver-neutral notification. Correction: the `0x4FA845B` /
`0x652294C7` pair the native `Flush` passes on device loss are the message server's service
IDs (`cRZSysServPtr<cIGZMessageServer2, 0x652294C7, 0x4FA845B>`), not a message.

### Open: intermittent hang/crash on SCD3D11 + AMD

Two of the first five runs with the generic renderer on SCD3D11 (AMD RX 7900 XTX, ReShade
add-on loaded) failed: an access violation in `amdxx32.dll` on an AMD worker thread after
about a minute, and a freeze within 10 s. In both the render thread was in our hook's
`driver->Flush()` → SCD3D11 → DXGI Present, waiting inside the AMD driver; no RS code was on
its stack (`FlushHook_` tail-calls `Flush`). Three later runs (renderer without panels, with
the demo windows, and with simulated mouse input for 40 s) stayed responsive at ~1,500 fps.
An earlier `amdxx32.dll` crash (2026-09-27) predates this work. Not attributed yet.

## Plan corrections found so far

- The repository state in the plan was out of date. The SCGL→SCD3D11 rename is
  committed on `feat/d3d11`. At the maintainer's request `feat/generic-backend` starts
  from `main` plus `vendor/imgui` at `v1.93.0-docking`, so the native SCD3D11 backend
  (`SCD3D11Hook`, the D3D11 texture path, `imgui_impl_dx11`) is **not** on this branch.
  SCD3D11 therefore uses the generic backend, which changes steps 4 and 6.
- The slot numbering caveat in the vtable section above.
- `ImGuiPlatformIO::Renderer_TextureMaxWidth`/`Height` are not new: they were added on
  2024-11-29 (`0f0473bf1`) and are in every release from v1.92.0, including the d3d7
  fork's 1.92.1. Docking is the reason for `v1.93.0-docking`, not these fields.
- The maintainer dropped native renderer backends altogether. The DX7 ImGui backend, its
  `EndScene` hook and the DirectDraw texture path were removed in step 2, so ImGui renders
  nothing until the generic backend lands in step 5. Step 6
  (`Backend=auto|generic|native`) no longer applies, and step 4's driver identification is
  only for logging and `GetActiveDriverInfo`. The D3DX capture stays for terrain-decal
  shadow recovery and `AcquireD3DInterfaces`.
- `CLAUDE.md`'s build commands: a multi-config Debug build fails to link
  (`gzcom_dll.lib` built with `/MDd`, everything else `/MD`) unless configured with
  `-DCMAKE_BUILD_TYPE=Debug`, as the CLion profile does. From Git Bash, MSBuild also
  needs `-p:Platform=Win32`.
