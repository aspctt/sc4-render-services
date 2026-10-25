// dear imgui: Renderer Backend for SimCity 4's cIGZGDriver
// See imgui_impl_gzgdriver.h for the feature list and call order.

#include "imgui_impl_gzgdriver.h"

#include "cIGZGDriver.h"
#include "utils/Logger.h"

#include <algorithm>
#include <cstddef>

namespace {
    // cIGZGDriver vocabulary. The interface takes plain integers. These are the values the
    // game and every driver agree on (see docs/generic-backend-re.md).
    constexpr uint32_t kVertexFormat_V3F_C4UB_T2F = 10;
    constexpr uint32_t kElementType_Vertices = 0;
    constexpr uint32_t kElementType_Color = 5;
    constexpr uint32_t kElementType_TexCoord = 7;
    constexpr uint32_t kPrimType_Triangles = 0;
    constexpr uint32_t kType_UnsignedByte = 1;
    constexpr uint32_t kType_UnsignedShort = 3;
    constexpr uint32_t kTextureTarget_2D = 0;
    constexpr int32_t kInternalFormat_RGBA8 = 4;
    constexpr uint32_t kTexFormat_BGRA = 3;
    constexpr uint32_t kTexParam_MagFilter = 0;
    constexpr uint32_t kTexParam_MinFilter = 1;
    constexpr uint32_t kTexParam_WrapU = 2;
    constexpr uint32_t kTexParam_WrapV = 3;
    constexpr int32_t kTexFilter_Nearest = 0;
    constexpr int32_t kTexFilter_Linear = 1;
    constexpr int32_t kTexWrap_Clamp = 2;
    constexpr uint32_t kTexEnvTarget = 0;
    constexpr uint32_t kBlend_SrcAlpha = 4;
    constexpr uint32_t kBlend_InvSrcAlpha = 5;
    constexpr uint32_t kMatrix_ModelView = 0;
    constexpr uint32_t kMatrix_Projection = 1;

    // The built-in DirectX 7 driver is bound by D3D7's limit. Later drivers allow more, but
    // ImGui never needs more than this for its atlas.
    constexpr auto kMaxTextureSize = 2048;

    // kGDVertexFormat_V3F_C4UB_T2F: position, BGRA8 diffuse, one UV set.
    struct GZVertex
    {
        float x, y, z;
        uint32_t bgra;
        float u, v;
    };
    static_assert(sizeof(GZVertex) == 24, "kGDVertexFormat_V3F_C4UB_T2F has a 24-byte stride");
    static_assert(sizeof(ImDrawIdx) == 2, "the backend submits 16-bit indices (kType_UnsignedShort)");

    struct ImGui_ImplGZGDriver_Data
    {
        cIGZGDriver* Driver = nullptr;
        ImVector<GZVertex> Vertices;    // One draw list, converted. Grows, never shrinks.
        ImVector<uint32_t> Upload;      // BGRA staging for texture uploads.
        int FramebufferWidth = 0;
        int FramebufferHeight = 0;
        intptr_t SavedTexture[2] = {0, 0};
        bool TexturesSaved = false;
        bool IsTextureProbed = false;   // Checked against the first texture this driver creates.
        bool IsTextureReliable = false;
    };

    ImGui_ImplGZGDriver_Data* GetBackendData() {
        return ImGui::GetCurrentContext()
                   ? static_cast<ImGui_ImplGZGDriver_Data*>(ImGui::GetIO().BackendRendererUserData)
                   : nullptr;
    }

    // ImGui packs colors as R,G,B,A bytes (0xAABBGGRR). The driver wants B,G,R,A (0xAARRGGBB).
    inline uint32_t RgbaToBgra(const uint32_t c) {
        return (c & 0xFF00FF00u) | ((c & 0x000000FFu) << 16) | ((c >> 16) & 0x000000FFu);
    }

    // The game's 2D convention (cGZGraphicSystem::SetUIRenderingMode): top-left origin, row-vector
    // layout with the translation in elements 12-14, depth fixed at 0.5, no half-pixel offset.
    void LoadOrtho(cIGZGDriver* driver, const float l, const float t, const float r, const float b) {
        const float m[16] = {
            2.0f / (r - l), 0.0f, 0.0f, 0.0f,
            0.0f, 2.0f / (t - b), 0.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 0.0f,
            (r + l) / (l - r), (t + b) / (b - t), 0.5f, 1.0f,
        };
        driver->LoadMatrix(m);
    }

    // Converts a rectangle of RGBA32 pixels into the BGRA staging buffer. Uploading BGRA keeps
    // the driver on its no-conversion path (SCD3D11 copies BGRA8 straight through).
    const uint32_t* StageBgra(ImGui_ImplGZGDriver_Data* bd, const void* pixels, const int w, const int h,
                              const int pitch) {
        bd->Upload.resize(w * h);
        uint32_t* dst = bd->Upload.Data;
        for (int row = 0; row < h; ++row) {
            const auto* src = reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(pixels) + row * pitch);
            for (int col = 0; col < w; ++col) {
                *dst++ = RgbaToBgra(src[col]);
            }
        }
        return bd->Upload.Data;
    }

    void UploadRect(ImGui_ImplGZGDriver_Data* bd, const uint32_t texture, const int x, const int y,
                    const int w, const int h, const void* pixels, const int pitch) {
        const uint32_t* bgra = StageBgra(bd, pixels, w, h, pitch);
        bd->Driver->LoadTextureLevel(texture, 0, x, y, w, h, kTexFormat_BGRA, kType_UnsignedByte,
                                     static_cast<uint32_t>(w), bgra);
    }

    uint32_t CreateDriverTexture(ImGui_ImplGZGDriver_Data* bd, const void* pixels, const int w, const int h,
                                 const int pitch) {
        if (w <= 0 || h <= 0 || w > kMaxTextureSize || h > kMaxTextureSize) {
            return 0;
        }
        // The game's own texture path, which every driver implements: CreateTexture allocates
        // and returns the name, LoadTextureLevel fills it. scvk implements neither GenTextures
        // nor TexImage2D, and the built-in driver's TexImage2D cannot take null pixels.
        // SCD3D11 and scvk bind the new texture to the active stage. RenderDrawData restores
        // the game's bindings afterwards.
        // Success is judged by the result and GetError, which every driver reads and clears.
        // The built-in driver returns a name even when allocation fails. Reading it first
        // also consumes an error the game left pending, which it never checks across frames.
        cIGZGDriver* driver = bd->Driver;
        driver->GetError();
        auto texture = static_cast<uint32_t>(driver->CreateTexture(kInternalFormat_RGBA8, static_cast<uint32_t>(w),
                                                                   static_cast<uint32_t>(h), 1, 0));
        if (driver->GetError() != 0 || texture == 0) {
            if (texture != 0) {
                driver->DeleteTextures(1, &texture);
            }
            return 0;
        }

        // IsTexture is only trusted if it recognizes a texture that was just created: scvk 0.2.2
        // answers from GenTextures' name space, which CreateTexture does not use.
        if (!bd->IsTextureProbed) {
            bd->IsTextureProbed = true;
            bd->IsTextureReliable = driver->IsTexture(texture);
            if (!bd->IsTextureReliable) {
                LOG_WARN("ImGui: this graphics driver cannot tell whether its textures still exist. If it ever "
                         "loses them, for example after switching away from the game, ImGui text may look wrong "
                         "until the game is restarted.");
            }
        }

        UploadRect(bd, texture, 0, 0, w, h, pixels, pitch);
        return texture;
    }

    void SetupRenderState(ImGui_ImplGZGDriver_Data* bd) {
        cIGZGDriver* driver = bd->Driver;

        // Stage 0: texture x vertex color. Stage 1 off.
        driver->TexStage(1);
        driver->Disable(kGDCapability_Texture2D);
        driver->TexStage(0);
        driver->Enable(kGDCapability_Texture2D);
        driver->TexStageMatrix(nullptr, 3, 2, 1);
        driver->TexStageCoord(0);
        driver->TexEnv(kTexEnvTarget, kGDTextureEnvParamType_Mode, kGDTextureEnvParam_Modulate);
        // Sampler state belongs to the stage, not the texture, so once per frame is enough.
        driver->TexParameter(kTextureTarget_2D, kTexParam_MagFilter, kTexFilter_Linear);
        driver->TexParameter(kTextureTarget_2D, kTexParam_MinFilter, kTexFilter_Linear);
        driver->TexParameter(kTextureTarget_2D, kTexParam_WrapU, kTexWrap_Clamp);
        driver->TexParameter(kTextureTarget_2D, kTexParam_WrapV, kTexWrap_Clamp);

        // Alpha blending, no depth, culling, alpha test, stencil or fog.
        driver->Disable(kGDCapability_DepthTest);
        driver->Disable(kGDCapability_AlphaTest);
        driver->Disable(kGDCapability_StencilTest);
        driver->Disable(kGDCapability_CullFace);
        driver->Disable(kGDCapability_Fog);
        driver->Enable(kGDCapability_Blend);
        driver->BlendFunc(kBlend_SrcAlpha, kBlend_InvSrcAlpha);
        driver->DepthMask(false);
        driver->ColorMask(true);
        driver->ColorMultiplier(1.0f, 1.0f, 1.0f);
        driver->AlphaMultiplier(1.0f);
        driver->EnableVertexColors(true, true);
        driver->PolygonOffset(0);

        // Model-view stays identity. Each clip rectangle loads its own projection.
        driver->MatrixMode(kMatrix_ModelView);
        driver->LoadIdentity();
        driver->MatrixMode(kMatrix_Projection);
    }

    // Puts back the state the game's own UI pass establishes (cGZGraphicSystem::SetUIRenderingMode),
    // since the driver state cannot be read back. Texture bindings are the exception: those are
    // restored exactly from GetTexture().
    void RestoreGameState(ImGui_ImplGZGDriver_Data* bd) {
        cIGZGDriver* driver = bd->Driver;

        if (bd->TexturesSaved) {
            driver->SetTexture(static_cast<uint32_t>(bd->SavedTexture[0]), 0);
            driver->SetTexture(static_cast<uint32_t>(bd->SavedTexture[1]), 1);
            bd->TexturesSaved = false;
        }

        driver->TexStage(1);
        driver->Disable(kGDCapability_Texture2D);
        driver->TexStage(0);
        driver->Enable(kGDCapability_Texture2D);
        driver->TexStageMatrix(nullptr, 3, 2, 1);
        driver->TexStageCoord(0);

        driver->Disable(kGDCapability_StencilTest);
        driver->Disable(kGDCapability_DepthTest);
        driver->Disable(kGDCapability_AlphaTest);
        driver->Disable(kGDCapability_Blend);
        driver->Disable(kGDCapability_CullFace);
        driver->Disable(kGDCapability_Fog);
        driver->AlphaMultiplier(1.0f);
        driver->ColorMultiplier(1.0f, 1.0f, 1.0f);
        driver->DepthMask(false);
        driver->ColorMask(true);
        driver->EnableVertexColors(true, true);
        driver->PolygonOffset(0);

        driver->SetViewport();
        driver->MatrixMode(kMatrix_Projection);
        LoadOrtho(driver, 0.0f, 0.0f, static_cast<float>(bd->FramebufferWidth),
                  static_cast<float>(bd->FramebufferHeight));
        driver->MatrixMode(kMatrix_ModelView);
        driver->LoadIdentity();
    }

    void UpdateTexture(ImGui_ImplGZGDriver_Data* bd, ImTextureData* tex) {
        if (tex->Status == ImTextureStatus_WantCreate) {
            IM_ASSERT(tex->TexID == ImTextureID_Invalid && tex->BackendUserData == nullptr);
            IM_ASSERT(tex->Format == ImTextureFormat_RGBA32);
            const uint32_t texture = CreateDriverTexture(bd, tex->GetPixels(), tex->Width, tex->Height, tex->GetPitch());
            if (texture == 0) {
                // Stays WantCreate, so the next frame retries.
                static bool s_logged = false;
                if (!s_logged) {
                    LOG_ERROR("ImGui: the graphics driver could not create a {}x{} texture, so ImGui text may "
                              "be missing. It will keep trying.", tex->Width, tex->Height);
                    s_logged = true;
                }
                return;
            }
            tex->SetTexID(static_cast<ImTextureID>(texture));
            tex->SetStatus(ImTextureStatus_OK);
        }
        else if (tex->Status == ImTextureStatus_WantUpdates) {
            const auto texture = static_cast<uint32_t>(tex->TexID);
            for (const ImTextureRect& r : tex->Updates) {
                UploadRect(bd, texture, r.x, r.y, r.w, r.h, tex->GetPixelsAt(r.x, r.y), tex->GetPitch());
            }
            tex->SetStatus(ImTextureStatus_OK);
        }
        if (tex->Status == ImTextureStatus_WantDestroy && tex->UnusedFrames > 0) {
            auto texture = static_cast<uint32_t>(tex->TexID);
            if (texture != 0) {
                bd->Driver->DeleteTextures(1, &texture);
            }
            tex->SetTexID(ImTextureID_Invalid);
            tex->SetStatus(ImTextureStatus_Destroyed);
        }
    }

    // Marks every texture as gone without calling the driver. ImGui recreates those still in use.
    void ForgetTextures() {
        for (ImTextureData* tex : ImGui::GetPlatformIO().Textures) {
            if (tex->Status != ImTextureStatus_Destroyed) {
                tex->SetTexID(ImTextureID_Invalid);
                tex->SetStatus(ImTextureStatus_Destroyed);
            }
        }
    }

    void SetSamplerFilter(const int32_t filter) {
        ImGui_ImplGZGDriver_Data* bd = GetBackendData();
        bd->Driver->TexParameter(kTextureTarget_2D, kTexParam_MagFilter, filter);
        bd->Driver->TexParameter(kTextureTarget_2D, kTexParam_MinFilter, filter);
    }

    void DrawCallback_SetSamplerLinear(const ImDrawList*, const ImDrawCmd*) { SetSamplerFilter(kTexFilter_Linear); }
    void DrawCallback_SetSamplerNearest(const ImDrawList*, const ImDrawCmd*) { SetSamplerFilter(kTexFilter_Nearest); }
    void DrawCallback_ResetRenderState(const ImDrawList*, const ImDrawCmd*) {}
}

bool ImGui_ImplGZGDriver_Init(cIGZGDriver* driver) {
    ImGuiIO& io = ImGui::GetIO();
    IMGUI_CHECKVERSION();
    IM_ASSERT(io.BackendRendererUserData == nullptr && "Already initialized a renderer backend!");

    if (!driver) {
        return false;
    }

    // Every driver implements format 10, but the layout is what this backend writes blindly.
    const uint32_t stride = driver->VertexFormatStride(kVertexFormat_V3F_C4UB_T2F);
    const uint32_t colorOffset = driver->VertexFormatElementOffset(kVertexFormat_V3F_C4UB_T2F, kElementType_Color, 0);
    const uint32_t uvOffset = driver->VertexFormatElementOffset(kVertexFormat_V3F_C4UB_T2F, kElementType_TexCoord, 0);
    const uint32_t posOffset = driver->VertexFormatElementOffset(kVertexFormat_V3F_C4UB_T2F, kElementType_Vertices, 0);
    if (stride != sizeof(GZVertex) || posOffset != offsetof(GZVertex, x) ||
        colorOffset != offsetof(GZVertex, bgra) || uvOffset != offsetof(GZVertex, u)) {
        LOG_ERROR("ImGui: this graphics driver lays out its vertices differently than expected (stride {}, "
                  "position {}, color {}, texture coordinates {}, expected 24, 0, 12 and 16), so ImGui cannot "
                  "draw with it.", stride, posOffset, colorOffset, uvOffset);
        return false;
    }

    auto* bd = IM_NEW(ImGui_ImplGZGDriver_Data)();
    bd->Driver = driver;
    io.BackendRendererUserData = bd;
    io.BackendRendererName = "imgui_impl_gzgdriver";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;  // We can honor the ImDrawCmd::VtxOffset field, allowing for large meshes.
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;   // We can honor ImGuiPlatformIO::Textures[] requests during render.

    ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
    platform_io.Renderer_TextureMaxWidth = platform_io.Renderer_TextureMaxHeight = kMaxTextureSize;
    platform_io.DrawCallback_ResetRenderState = DrawCallback_ResetRenderState;
    platform_io.DrawCallback_SetSamplerLinear = DrawCallback_SetSamplerLinear;
    platform_io.DrawCallback_SetSamplerNearest = DrawCallback_SetSamplerNearest;
    return true;
}

void ImGui_ImplGZGDriver_Shutdown() {
    ImGui_ImplGZGDriver_Data* bd = GetBackendData();
    IM_ASSERT(bd != nullptr && "No renderer backend to shutdown, or already shutdown?");
    ImGuiIO& io = ImGui::GetIO();

    ForgetTextures();
    io.BackendRendererName = nullptr;
    io.BackendRendererUserData = nullptr;
    io.BackendFlags &= ~(ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures);
    ImGui::GetPlatformIO().ClearRendererHandlers();
    IM_DELETE(bd);
}

void ImGui_ImplGZGDriver_NewFrame(cIGZGDriver* driver) {
    ImGui_ImplGZGDriver_Data* bd = GetBackendData();
    IM_ASSERT(bd != nullptr && "Context or backend not initialized! Did you call ImGui_ImplGZGDriver_Init()?");

    if (driver != bd->Driver) {
        // Textures belong to the old driver, which may be gone: forget them, never delete them.
        ForgetTextures();
        bd->Driver = driver;
        bd->IsTextureProbed = false;
        bd->IsTextureReliable = false;
    }

    // A driver may drop textures (e.g. on device loss). Hand those back to ImGui, which recreates
    // them from the pixels it keeps.
    if (bd->IsTextureReliable) {
        for (ImTextureData* tex : ImGui::GetPlatformIO().Textures) {
            if (tex->Status == ImTextureStatus_OK && !driver->IsTexture(static_cast<uint32_t>(tex->TexID))) {
                tex->SetTexID(ImTextureID_Invalid);
                tex->SetStatus(ImTextureStatus_Destroyed);
            }
        }
    }

    // The framebuffer is what the driver presents, which need not match the window's client
    // area that the platform backend reports as DisplaySize.
    int32_t viewport[4] = {};
    driver->SetViewport();
    driver->GetViewport(viewport);
    bd->FramebufferWidth = viewport[2] - viewport[0];
    bd->FramebufferHeight = viewport[3] - viewport[1];
    ImGuiIO& io = ImGui::GetIO();
    if (io.DisplaySize.x > 0.0f && io.DisplaySize.y > 0.0f && bd->FramebufferWidth > 0 && bd->FramebufferHeight > 0) {
        io.DisplayFramebufferScale = ImVec2(static_cast<float>(bd->FramebufferWidth) / io.DisplaySize.x,
                                            static_cast<float>(bd->FramebufferHeight) / io.DisplaySize.y);
    }

    // Saved before any panel can create a texture, which rebinds stage 0.
    bd->SavedTexture[0] = driver->GetTexture(0);
    bd->SavedTexture[1] = driver->GetTexture(1);
    bd->TexturesSaved = true;
}

void ImGui_ImplGZGDriver_RenderDrawData(ImDrawData* draw_data) {
    ImGui_ImplGZGDriver_Data* bd = GetBackendData();
    cIGZGDriver* driver = bd->Driver;

    // Avoid rendering when minimized, but always put the game's state back.
    const int fb_width = static_cast<int>(draw_data->DisplaySize.x * draw_data->FramebufferScale.x);
    const int fb_height = static_cast<int>(draw_data->DisplaySize.y * draw_data->FramebufferScale.y);
    if (fb_width <= 0 || fb_height <= 0 || bd->FramebufferWidth <= 0 || bd->FramebufferHeight <= 0) {
        RestoreGameState(bd);
        return;
    }

    // Catch up with texture updates. Most of the times, the list will have 1 element with an OK status, aka nothing to do.
    if (draw_data->Textures != nullptr) {
        for (ImTextureData* tex : *draw_data->Textures) {
            if (tex->Status != ImTextureStatus_OK) {
                UpdateTexture(bd, tex);
            }
        }
    }

    SetupRenderState(bd);

    ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
    ImGui_ImplGZGDriver_RenderState render_state{driver};
    platform_io.Renderer_RenderState = &render_state;

    // Clipping uses the viewport: it doubles as the scissor on every driver, and a projection
    // spanning exactly the clip rectangle keeps ImGui's pixel grid aligned with the screen.
    const ImVec2 clip_off = draw_data->DisplayPos;
    const ImVec2 clip_scale = draw_data->FramebufferScale;
    const int fb_h = bd->FramebufferHeight;
    int last_clip[4] = {-1, -1, -1, -1};
    ImTextureID last_texture = ImTextureID_Invalid;
    bool texture_bound = false;  // 0 is a valid binding too, so track "never bound" apart

    for (const ImDrawList* draw_list : draw_data->CmdLists) {
        // Convert each list once. Every command then points into it.
        const int vtx_count = draw_list->VtxBuffer.Size;
        bd->Vertices.resize(vtx_count);
        GZVertex* dst = bd->Vertices.Data;
        for (const ImDrawVert& src : draw_list->VtxBuffer) {
            *dst++ = GZVertex{src.pos.x, src.pos.y, 0.0f, RgbaToBgra(src.col), src.uv.x, src.uv.y};
        }
        const GZVertex* vtx_base = bd->Vertices.Data;
        const ImDrawIdx* idx_base = draw_list->IdxBuffer.Data;
        unsigned int last_vtx_offset = ~0u;

        for (const ImDrawCmd& cmd : draw_list->CmdBuffer) {
            if (cmd.UserCallback != nullptr) {
                // User callback, registered via ImDrawList::AddCallback()
                if (cmd.UserCallback == DrawCallback_ResetRenderState) {
                    SetupRenderState(bd);
                }
                else {
                    cmd.UserCallback(draw_list, &cmd);
                }
                // A callback may have changed anything, so issue everything again.
                last_clip[0] = -1;
                texture_bound = false;
                last_vtx_offset = ~0u;
                continue;
            }
            if (cmd.ElemCount == 0) {
                continue;
            }

            // Project the clipping rectangle into framebuffer space and clamp it to the screen.
            const int x0 = (std::max)(static_cast<int>((cmd.ClipRect.x - clip_off.x) * clip_scale.x), 0);
            const int y0 = (std::max)(static_cast<int>((cmd.ClipRect.y - clip_off.y) * clip_scale.y), 0);
            const int x1 = (std::min)(static_cast<int>((cmd.ClipRect.z - clip_off.x) * clip_scale.x), bd->FramebufferWidth);
            const int y1 = (std::min)(static_cast<int>((cmd.ClipRect.w - clip_off.y) * clip_scale.y), fb_h);
            if (x1 <= x0 || y1 <= y0) {
                continue;
            }

            if (x0 != last_clip[0] || y0 != last_clip[1] || x1 != last_clip[2] || y1 != last_clip[3]) {
                // SetViewport takes a bottom-left origin.
                driver->SetViewport(x0, fb_h - y1, x1 - x0, y1 - y0);
                LoadOrtho(driver,
                          clip_off.x + static_cast<float>(x0) / clip_scale.x,
                          clip_off.y + static_cast<float>(y0) / clip_scale.y,
                          clip_off.x + static_cast<float>(x1) / clip_scale.x,
                          clip_off.y + static_cast<float>(y1) / clip_scale.y);
                last_clip[0] = x0;
                last_clip[1] = y0;
                last_clip[2] = x1;
                last_clip[3] = y1;
            }

            const ImTextureID texture = cmd.GetTexID();
            if (!texture_bound || texture != last_texture) {
                driver->SetTexture(static_cast<uint32_t>(texture), 0);
                last_texture = texture;
                texture_bound = true;
            }

            if (cmd.VtxOffset != last_vtx_offset) {
                driver->InterleavedArrays(kVertexFormat_V3F_C4UB_T2F, 0, vtx_base + cmd.VtxOffset);
                last_vtx_offset = cmd.VtxOffset;
            }

            driver->DrawElements(kPrimType_Triangles, static_cast<int32_t>(cmd.ElemCount), kType_UnsignedShort,
                                 idx_base + cmd.IdxOffset);
        }
    }

    platform_io.Renderer_RenderState = nullptr;
    RestoreGameState(bd);
}

uint32_t ImGui_ImplGZGDriver_CreateTexture(const void* rgba_pixels, const int width, const int height) {
    ImGui_ImplGZGDriver_Data* bd = GetBackendData();
    if (!bd || !rgba_pixels) {
        return 0;
    }
    return CreateDriverTexture(bd, rgba_pixels, width, height, width * 4);
}

void ImGui_ImplGZGDriver_DestroyTexture(uint32_t texture) {
    ImGui_ImplGZGDriver_Data* bd = GetBackendData();
    if (bd && texture != 0) {
        bd->Driver->DeleteTextures(1, &texture);
    }
}

bool ImGui_ImplGZGDriver_IsTextureAlive(const uint32_t texture) {
    ImGui_ImplGZGDriver_Data* bd = GetBackendData();
    if (!bd || texture == 0) {
        return false;
    }
    // Without a trustworthy IsTexture, assume a texture lives until it is destroyed.
    return !bd->IsTextureReliable || bd->Driver->IsTexture(texture);
}
