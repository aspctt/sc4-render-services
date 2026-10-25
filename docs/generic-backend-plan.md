# Handoff: driver-agnostic ImGui backend for SC4RenderServices

You are taking over a planned refactor of this repository (SC4RenderServices, "RS"; GitHub
`caspervg/sc4-render-services`, local folder `sc4-imgui-backend`). The design decisions below were
made with the maintainer in an earlier session. Do not reopen them unless you find evidence they
cannot work; if you do, stop and say so.

Work through the steps in order. Each step ends with a check. Stop and report to the user at the
end of each step, and whenever a step needs their eyes in the game.

---

## Goal

Make RS work identically whichever renderer plugin is active (the game's built-in DirectX 7
driver, SCD3D11, scvk, or a future one), and make it self-contained:

1. A new ImGui renderer backend that draws through the game's own `cIGZGDriver` interface
   instead of through D3D7, D3D11 or Vulkan. Every renderer plugin has to implement that
   interface, because the game draws its own UI with it.
2. ImGui moves to the **docking** branch.
3. ImGui is compiled into `SC4RenderServices.dll` and exported from it. `imgui.dll` in the game's
   `Apps\` folder goes away.
4. RS identifies the active driver reliably and picks a backend automatically.

## Out of scope for this handoff (later phases, but do not design them out)

- A backend-neutral world-overlay API on `DrawService` (`DrawWorldTriangles`/`DrawWorldLines`
  drawn through `cIGZGDriver` inside the existing draw-pass hooks), and porting
  SC4PlopAndPaint and SC4TerrainExtensions onto it so they drop their own D3D7/D3D11 code.
- ImGui multi-viewports (secondary OS windows). Impossible through `cIGZGDriver`; a possible
  native-only extra on SCD3D11 later.
- A shared driver extension interface (queried through `cIGZGDriver::QueryInterface`) that
  SCD3D11 and scvk would both implement. Needs the scvk author.
- Terrain-decal "shadow recovery" (`src/service/decal/TerrainDecalHook.cpp`) creates DX7
  offscreen surfaces. `cIGZGDriver` has no render-to-texture, so it stays native for now.
- Do not modify the other repositories (scgl/SCD3D11, sc4-advanced-plop,
  sc4-terrain-extensions).

---

## Established facts (with sources)

Treat "verified" as checked in source; "inferred" still needs confirming.

**`cIGZGDriver`**
- Two header copies exist. `vendor/gzcom-dll/gzcom-dll/include/cIGZGDriver.h` is the one RS
  builds against. `C:\Users\caspe\CLionProjects\scgl\scd3d11\cIGZGDriver.h` is SCD3D11's copy,
  annotated with Windows vtable offsets. It also notes that MSVC lays out same-name virtual
  overloads in reverse declaration order (`TexStageCombine`, `SetViewport`). Before calling any
  overloaded method, check RS's copy against the annotated one. A wrong slot calls the wrong
  method.
- `Flush()` is the game's frame boundary. SCD3D11 presents there
  (`scgl/scd3d11/cGDriver_Viewport.cpp:524`), and per the contract inventory the native D3D7
  driver "ends queued scene work, presents, restarts scene work" there too. Verified for
  SCD3D11; documented for native.
- `GetInteger`/`GetFloat`/`GetBoolean` are effectively stubs: SCD3D11 answers only
  `pname == 0`, the pixel-store row length (`scgl/scd3d11/cGDriver.cpp:438`). **You cannot read
  render state back.** This is the main design constraint; see step 1.
- Vertex format `kGDVertexFormat_V3F_C4UB_T2F = 10` (position 3×float, BGRA8 colour, 1 UV) is a
  standard game format (`scgl/scd3d11/VertexFormatUtils.h`). Client-memory
  `InterleavedArrays` + `DrawElements` is the game's main submission path.
- Textures: RGBA8 internal format and RGBA/BGRA upload are used by the game.
  `LoadTextureLevel` supports sub-rectangle updates with an explicit row length.
  `GenTextures`/`DeleteTextures`/`IsTexture` exist.
- `SetViewport(x, y, w, h)` sets both viewport and scissor in SCD3D11
  (`cGDriver_Viewport.cpp:626`). Its Y is bottom-left and the native DX driver converts it.
  `GetViewport` returns left/top/right/bottom.
- The full contract inventory is `C:\Users\caspe\CLionProjects\scgl\docs\d3d11-contract-inventory.md`
  (local only; gitignored there). Read its "Frame", "Textures", "Vertex", "Matrices" and
  "Render state" sections before step 5.

**Drivers and identification**
- The game's DirectX class ID is `0xBADB6906` (`kSCGDriverDirectX`); OpenGL is `0xC4554841`.
- SCD3D11 registers on the DirectX class ID at version 1000000. Its module is `SCD3D11.dll`, and
  it exports `SCD3D11RegisterFrameCallback`/`SCD3D11UnregisterFrameCallback`.
- scvk (`github.com/aspctt/scvk`): its active driver reports the OpenGL class ID. It also claims
  the DirectX slot one version below SCD3D11. Its module is `scvk.dll`, and it exports
  `SCVKRegisterFrameCallback`.
- RS's current `SCD3D11Hook::IsSCD3D11Driver` checks "DirectX class ID **and**
  `GetModuleHandle("SCD3D11.dll")`". With SCD3D11 and scvk both installed and scvk active, that
  module check passes even though SCD3D11 isn't drawing. Replace it (step 4).

**Existing RS surfaces you will reuse**
- `src/service/DrawService.cpp` already hooks the game's draw passes at exe call sites
  (`0x007CB770`…) and has thunks into the game's `SC4DrawContext`. These include
  `SetDefaultRenderState` `0x007D5200` and `SetDefaultRenderStateUnilaterally` `0x007D5230`.
  The game keeps its own render-state cache above the driver. That is why the "unilaterally"
  variant exists, and it is what you will use to resynchronise after drawing.
- `src/GraphicsInterfaceHook.*` (DX7: grabs the D3DX object at driver `+0x24C`, hooks
  `EndScene`), and `src/SCD3D11Hook.*` (D3D11 frame callback).

**ImGui today**
- `vendor/imgui` and `vendor/d3d7imgui/ImGui` are both v1.92.1 (not docking). The `imgui`
  SHARED target in `CMakeLists.txt` builds the **core from `vendor/d3d7imgui/ImGui`**, plus
  `imgui_impl_dx11` from `vendor/imgui/backends`.
- Client plugins link `imgui.lib` and rely on the single `GImGui` inside `imgui.dll`. They do not
  call `SetCurrentContext`.
- `ImGuiServiceIds.h`: service `0xA4F2D0C1`, interface `GZIID_cIGZImGuiService = 0x9B6F8E21`,
  `kImGuiServiceApiVersion = 4`.
- `imgui_impl_dx7.cpp` uses the legacy font path (`GetTexDataAsRGBA32`) and software
  Sutherland–Hodgman clipping. Its header says viewport juggling "caused ghost UIs". The clipper
  is plain code and can be reused.

**Repository state when this was written**
- Branch `feat/d3d11`, with **uncommitted** changes: the SCGL→SCD3D11 rename (`SCGLD3D11Service.h`,
  `SCGLDX11Hook.*` → `SCD3D11Service.h`, `SCD3D11Hook.*`, plus edits in `CMakeLists.txt`,
  `cIGZImGuiService.h` and `ImGuiService.*`). `feat/d3d11` is 3 commits ahead of `main`.
- `CLAUDE.md` is out of date (`SC4ImGuiService.dll`, `DX7InterfaceHook`). Fix it in step 8.

---

## Step 0: Orient and branch

1. Run `git status` and `git log --oneline -5`. If the rename above is still uncommitted, **ask
   the user** to commit it or to let you commit it. Do not stash, discard or build on top of it
   uncommitted.
2. Create `feat/generic-backend` from the committed `feat/d3d11`.
3. Build Debug and Release (see `CLAUDE.md`; 32-bit only) to get a baseline.

Check: a clean baseline build on the new branch.

## Step 1: Reverse engineering (no code)

Use the `sc4-reverse-engineering` skill and `ghx`. Run `ghx doctor` first. In the previous
session only `SimCity 4.exe` was open, and `ghx dec 0x007D5230` returned
`404 No context found`. If that happens again, ask the user to check the Ghidra bridge and
whether to open the Mac reference binary. Do not probe the bridge with raw HTTP calls.

Answer the following questions and record them, with addresses, prologue bytes and confidence, in
`docs/generic-backend-re.md`:

1. **Per-frame `Flush` call site(s).** Where does the game call `cIGZGDriver::Flush` once per
   displayed frame? Hook that call site in the exe, not the driver's vtable slot, so ImGui is not
   drawn into region thumbnails, snapshots or photos. List any other `Flush` callers you find.
   Virtual calls cannot be enumerated completely, so say so.
2. **The `SC4DrawContext` state cache.** What do `SetDefaultRenderState` (`0x007D5200`) and
   `SetDefaultRenderStateUnilaterally` (`0x007D5230`) cover? Blend, depth, alpha test, cull, fog,
   both texture stages and combiners, bound textures, vertex colour flags, colour/alpha
   multipliers, matrices? Can you get the active draw context at `Flush` time
   (`getDrawContext` thunk `0x004E82A0`)? Does the game reload projection and model-view every
   frame, or does it assume they persist?
3. **The game's own 2D UI projection.** How does the game set up its 2D UI drawing (matrix and
   viewport)? ImGui's projection should copy it so text stays pixel-sharp on every driver.

Decide between two strategies for state hygiene:
- **(A)** After ImGui draws, resynchronise through the game's own cache with
  `SetDefaultRenderStateUnilaterally` and reload any matrices it doesn't cover. Preferred if
  question 2 shows the coverage is sufficient.
- **(B)** Shadow the state: hook the active driver's state-setter vtable slots, record the last
  values, and restore them exactly. Use this only if (A) doesn't cover enough.

Check: the RE doc exists; the user agrees with the hook point and the chosen strategy.

## Step 2: Move ImGui to the docking branch

1. Pick the docking tag of an ImGui release that has `ImGuiPlatformIO::Renderer_TextureMaxWidth`/
   `Renderer_TextureMaxHeight`. These are absent in 1.92.1; confirm which release added them, and
   don't guess. If the right version is unclear, ask the user.
2. Point `vendor/imgui` at that tag. Build the `imgui` core from `vendor/imgui`, not from
   `vendor/d3d7imgui/ImGui`. Keep compiling `imgui_impl_dx7.cpp` against the new headers, and
   keep `imgui_impl_win32` from `vendor/imgui/backends`.
3. Enable `ImGuiConfigFlags_DockingEnable` in `ImGuiService`. Choose and document where the
   layout `.ini` lives; add a setting in `SC4RenderServices.ini` if useful.

Check: the DX7 and D3D11 paths still work in game (user verifies), and docking works with the
sample panels.

## Step 3: Fold ImGui into `SC4RenderServices.dll`

1. Compile ImGui core and backends into `SC4RenderServices`. On RS's side, define `IMGUI_API` as
   `__declspec(dllexport)`; on clients' side as `__declspec(dllimport)`. Drop the `imgui` SHARED
   target and `imgui.dll` deployment.
2. Clients link `SC4RenderServices.lib` with `/DELAYLOAD:SC4RenderServices.dll` (plus
   `delayimp.lib`). This is what makes load order irrelevant. If the game loads plugins
   alphabetically, `SC4PlopAndPaint.dll` loads before `SC4RenderServices.dll`, and a normal
   import would fail. The delay-load helper resolves on the first ImGui call, which happens after
   `PostAppInit`, and the loader reuses the already-loaded module. Provide a CMake helper or
   snippet clients can use. Add a delay-load failure hook (`__pfnDliFailureHook2`) that logs
   instead of crashing.
3. Convert every sample in `src/sample/` to this scheme. They are the proof that it works.
4. Migration safety:
   - Introduce a new interface ID (e.g. `GZIID_cIGZImGuiService2`) and set
     `kImGuiServiceApiVersion = 5`. **Stop serving the old interface ID.** Old plugins, which
     still import an old `imgui.dll`, then fail to find the service and run without UI instead of
     using RS's context from a different ImGui version.
   - Log a warning at startup if `imgui.dll` is loaded in the process or present next to the
     game exe.

Check: with no `imgui.dll` anywhere, all samples work in game. An old plugin binary, if the user
has one handy, degrades cleanly.

## Step 4: Identify the active driver

1. Identify the driver by vtable owner:
   `GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | ..._UNCHANGED_REFCOUNT, *(void**)pDriver)`.
   Map `SimCity 4.exe` + DirectX class ID to native DX7, `SCD3D11.dll` to SCD3D11, `scvk.dll` to
   scvk, and anything else to unknown.
2. Replace `SCD3D11Hook::IsSCD3D11Driver` and the class-ID-only checks in `ImGuiService` with
   this. Log the result once, with the module path and class ID, using a clear message such as
   `active renderer: scvk (Vulkan) — using generic backend`.
3. Add a small query (e.g. `GetActiveDriverInfo`) to the public API so plugins can log or adapt.

Check: correct identification in all three setups, including SCD3D11 and scvk installed together
with scvk selected through sc4-graphics-options `Driver=`.

## Step 5: The generic backend

Create `src/backends/imgui_impl_gzgdriver.{h,cpp}`. Model it on **`imgui_impl_dx11.cpp`** (its
`RenderDrawData`/`SetupRenderState`/`UpdateTexture`/`RenderState` structure), not on the DX7
backend.

- Backend flags: `RendererHasVtxOffset` and `RendererHasTextures`. Set
  `Renderer_TextureMaxWidth`/`Height` from what the driver reports. The built-in DX7 driver may
  report D3D7's 2048 limit.
- Vertices: copy each frame into `V3F_C4UB_T2F`, swizzling ImGui's RGBA to BGRA. Do **not**
  define `IMGUI_USE_BGRA_PACKED_COLOR`, because it changes the ABI every client sees. 16-bit
  indices via `DrawElements`. Apply `VtxOffset` by offsetting the `InterleavedArrays` pointer.
- Projection: an orthographic projection from `DisplayPos`/`DisplaySize`/`FramebufferScale`
  through `MatrixMode` + `LoadMatrix`, matching the game's 2D convention from step 1.
  Model-view is identity.
- State: blend SrcAlpha/InvSrcAlpha; no depth test or write, no cull, alpha test or fog;
  stage 0 modulate (texture × vertex colour); stage 1 disabled; linear filtering, clamp.
- Clipping: per command, `SetViewport(rect)` plus a matching orthographic matrix. Keep the DX7
  backend's CPU clipper behind a switch (e.g. `GenericClipMode=viewport|cpu`) in case a driver
  misbehaves.
- Textures: `WantCreate` → `GenTextures` + `TexImage2D` (RGBA8). `WantUpdates` →
  `LoadTextureLevel` per `tex->Updates[]` rectangle, with a full-level re-upload fallback.
  `WantDestroy` → `DeleteTextures`. `ImTextureID` is the driver texture name. Each frame, check
  `IsTexture` on live textures; if a video-mode change or device loss removed them, set them back
  to `WantCreate`.
- `Renderer_RenderState`: expose `{ cIGZGDriver* }` (and later anything native) to draw
  callbacks; handle `ImDrawCallback_ResetRenderState`.
- Hook: draw at the per-frame `Flush` call site from step 1, then apply the step 1 state
  strategy.
- RS's own texture API (`CreateTexture`/`GetTextureID` paths in `ImGuiService.cpp`, currently
  `backend_ == D3D11` vs DDraw surfaces) needs a generic branch on the same primitives.

Check: builds; behind `Backend=generic` it renders the samples on at least one driver (user
verifies).

## Step 6: Backend selection

- Add an `SC4RenderServices.ini` setting `Backend=auto|generic|native`. `auto` uses native for
  built-in DX7 and SCD3D11 (known good) and generic for scvk and unknown drivers. The default
  stays `auto`, so current users see no change.
- Make switching robust: only one renderer backend is ever initialised, and device or
  video-mode changes reinitialise the right one.

## Step 7: Test matrix (needs the user in game)

Run every row on built-in DX7, SCD3D11 and scvk, with `Backend=generic`, and the native rows as
regression:

- Text sharpness and clipping (scroll lists, tables, nested child windows); docking, including
  drag, tab, undock inside the window and saving the layout.
- Textures: `ImGuiTextureSample`; font atlas growth (large font size, many glyphs); a large
  UI scale.
- **The game's own rendering the frame after ImGui**: city at day and night, the game's own UI,
  tooltips, zoning and network drag previews, region view. A wrong frame here means state
  hygiene is broken.
- Alt+Tab, resolution change, windowed/borderless/fullscreen, region↔city, loading screens,
  in-game photo, region thumbnail on save (ImGui must not appear in it).
- Performance: frame time with the demo window open, compared with native. With SCD3D11, watch
  its geometry upload cache (xxHash fingerprints): ImGui geometry changes every frame and must
  not thrash it.

The user can drive the game; ask them for screenshots or log excerpts rather than assuming a
result.

## Step 8: Documentation and release preparation

- Update `README.md` installation: no `Apps\imgui.dll` any more; delete the old one.
  `SC4RenderServices.dll` and `.ini` go in `Plugins\`. Document `Backend=` and the renderer
  support matrix.
- Write migration notes for plugin authors: new headers (docking), link
  `SC4RenderServices.lib` with `/DELAYLOAD`, new interface ID and API version 5, a rebuild is
  required, and `GetActiveDriverInfo`.
- Rewrite `CLAUDE.md` to match the current architecture.
- Do not tag or publish a release; the maintainer will coordinate RS, Plop and Paint and Terrain
  Extensions releases.

---

## Working rules

- Match the existing code style (comment density, naming, logging through `utils/Logger.h`).
- Commit per step with clear messages, on `feat/generic-backend` only. Don't push without being
  asked.
- When something above turns out wrong (an address, a slot, a driver behaviour), record the
  correction in `docs/generic-backend-re.md` and tell the user. Don't silently work around it.
