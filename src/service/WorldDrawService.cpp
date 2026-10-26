#include "WorldDrawService.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

#include "DrawService.h"
#include "GZServPtrs.h"
#include "ImGuiService.h"
#include "SC4UI.h"
#include "cIGZGDriver.h"
#include "cISC43DRender.h"
#include "cISC4App.h"
#include "cISC4City.h"
#include "cISTETerrain.h"
#include "cS3DVector3.h"
#include "public/SC4WorldMeshBuilder.h"
#include "utils/Logger.h"
#include "utils/VersionDetection.h"

namespace {
    // SC4DrawContext and cS3DCamera, SimCity 4 641. See docs/world-draw-design.md for how each
    // was checked in Ghidra.
    using GetDrawContextFn = void* (__thiscall*)(void* renderer);
    using ContextFn = void (__thiscall*)(void* context);
    using ContextBoolFn = void (__thiscall*)(void* context, bool value);
    using ContextCharFn = void (__thiscall*)(void* context, char value);
    using ContextIntFn = void (__thiscall*)(void* context, int value);
    using ContextUIntFn = void (__thiscall*)(void* context, uint32_t value);
    using ContextUIntPairFn = void (__thiscall*)(void* context, uint32_t a, uint32_t b);
    using ContextStageFlagFn = void (__thiscall*)(void* context, char enable, int stage);
    using ContextStageUIntFn = void (__thiscall*)(void* context, uint32_t value, int stage);
    using SetCameraFn = void (__thiscall*)(void* context, cS3DCamera* camera);
    using SetFogFn = void (__thiscall*)(void* context, bool enable, float* color, float start, float end);
    using DrawPrimsFn = void (__thiscall*)(void* context, uint32_t prim, uint32_t format, uint32_t count,
                                           const void* vertices);
    using ProjectFn = bool (__thiscall*)(cS3DCamera* camera, const cS3DVector3& in, cS3DVector3& out);

    const auto kGetDrawContext = reinterpret_cast<GetDrawContextFn>(0x004E82A0);
    const auto kSetDefaultRenderStateUnilaterally = reinterpret_cast<ContextFn>(0x007D5230);
    const auto kSetCamera = reinterpret_cast<SetCameraFn>(0x007D3D80);
    const auto kSetModelViewTransformChanged = reinterpret_cast<ContextFn>(0x007D2750);
    const auto kResetModelViewTransform = reinterpret_cast<ContextFn>(0x007D2D20);
    const auto kSetLighting = reinterpret_cast<ContextBoolFn>(0x007D30D0);
    const auto kResetShade = reinterpret_cast<ContextFn>(0x007D3290);
    const auto kEnableTextureStateFlag = reinterpret_cast<ContextStageFlagFn>(0x007D3EF0);
    const auto kSetTexture = reinterpret_cast<ContextStageUIntFn>(0x007D4070);
    const auto kSetTexEnvMode = reinterpret_cast<ContextStageUIntFn>(0x007D40B0);
    const auto kEnableBlendStateFlag = reinterpret_cast<ContextCharFn>(0x007D4010);
    const auto kEnableAlphaTestFlag = reinterpret_cast<ContextBoolFn>(0x007D3E90);
    const auto kEnableCullFaceFlag = reinterpret_cast<ContextCharFn>(0x007D2850);
    const auto kEnableDepthMaskFlag = reinterpret_cast<ContextBoolFn>(0x007D2800);
    const auto kEnableDepthTestFlag = reinterpret_cast<ContextCharFn>(0x007D27B0);
    const auto kSetBlendFunc = reinterpret_cast<ContextUIntPairFn>(0x007D28F0);
    const auto kSetDepthFunc = reinterpret_cast<ContextUIntFn>(0x007D28A0);
    const auto kSetDepthOffset = reinterpret_cast<ContextIntFn>(0x007D4480);
    const auto kSetFog = reinterpret_cast<SetFogFn>(0x007D32D0);
    const auto kDrawPrims = reinterpret_cast<DrawPrimsFn>(0x007D2990);
    const auto kCameraProject = reinterpret_cast<ProjectFn>(0x007FFF10);
    const auto kCameraUnProject = reinterpret_cast<ProjectFn>(0x00800120);

    constexpr ptrdiff_t kContextDriverOffset = 0x30;
    constexpr ptrdiff_t kContextFogEnabledOffset = 0xA0;
    constexpr ptrdiff_t kContextDepthOffsetOffset = 0xBC;
    constexpr ptrdiff_t kContextShadeAppliedOffset = 0x1F8;

    constexpr uint32_t kPrimTriangles = 0;
    constexpr uint32_t kVertexFormatPositionColorUV = 10;
    constexpr uint32_t kIndexTypeUInt16 = 3;
    constexpr uint32_t kTexEnvModulate = 1;
    constexpr uint32_t kDepthLessEqual = 3;
    constexpr uint32_t kBlendZero = 0;
    constexpr uint32_t kBlendOne = 1;
    constexpr uint32_t kBlendSrcAlpha = 4;
    constexpr uint32_t kBlendInvSrcAlpha = 5;
    constexpr uint32_t kBlendDstColor = 8;

    // The built-in DirectX driver copies each draw into a 64 KB dynamic vertex buffer
    // (0x00889910) and silently drops a draw that does not fit (0x008899B0). It stores the
    // vertices in its own layout, up to 32 bytes each on that path, so 2046 always fits and is
    // a whole number of triangles.
    constexpr uint32_t kMaxVertexBytesOnBuiltInDriver = 32;
    constexpr uint32_t kMaxVerticesPerDraw = 0x10000 / kMaxVertexBytesOnBuiltInDriver / 3 * 3;

    template <typename T>
    T& Field(void* object, const ptrdiff_t offset) {
        return *reinterpret_cast<T*>(static_cast<std::byte*>(object) + offset);
    }

    void* ActiveRenderer() {
        const auto view3DWin = SC4UI::GetView3DWin();
        return view3DWin ? view3DWin->GetRenderer() : nullptr;
    }

    // One pass worth of world drawing on the game's draw context. Sets the camera up once,
    // applies each batch's style through the draw context so its state cache stays right, and
    // hands the game back a known state at the end.
    class WorldFrame final : public cIGZWorldDrawContext
    {
    public:
        WorldFrame(WorldDrawService& service, const DrawServicePass pass, void* context, cS3DCamera* camera)
            : service_(service)
            , pass_(pass)
            , context_(context)
            , driver_(Field<cIGZGDriver*>(context, kContextDriverOffset))
            , camera_(camera) {}

        void Begin() {
            sceneFog_ = Field<uint8_t>(context_, kContextFogEnabledOffset) != 0;
            previousDepthOffset_ = Field<int>(context_, kContextDepthOffsetOffset);

            kSetDefaultRenderStateUnilaterally(context_);
            if (camera_) {
                kSetCamera(context_, camera_);
            }
            // An identity model transform: the view alone, the way the game resets it.
            kSetModelViewTransformChanged(context_);
            kResetModelViewTransform(context_);
            kSetLighting(context_, false);
            kEnableAlphaTestFlag(context_, false);
            kEnableTextureStateFlag(context_, false, 1);

            // Full-strength colours. The draw context restores its own shade in End through
            // ResetShade, which only acts while this flag is set.
            kResetShade(context_);
            if (driver_) {
                driver_->ColorMultiplier(1.0f, 1.0f, 1.0f);
                driver_->AlphaMultiplier(1.0f);
                driver_->EnableVertexColors(true, true);
            }
            Field<uint8_t>(context_, kContextShadeAppliedOffset) = 1;
        }

        void End() {
            kSetDepthOffset(context_, previousDepthOffset_);
            kSetFog(context_, sceneFog_, nullptr, FLT_MAX, FLT_MAX);
            kResetShade(context_);
            kSetDefaultRenderStateUnilaterally(context_);
            kSetModelViewTransformChanged(context_);
        }

        [[nodiscard]] DrawServicePass GetPass() const override { return pass_; }

        void DrawTriangles(const SC4WorldStyle& style, const SC4WorldVertex* vertices,
                           const uint32_t vertexCount) override {
            if (!vertices || vertexCount < 3 || !ApplyStyle_(style)) {
                return;
            }
            const uint32_t usable = vertexCount - vertexCount % 3;
            for (uint32_t first = 0; first < usable; first += kMaxVerticesPerDraw) {
                const uint32_t count = (std::min)(kMaxVerticesPerDraw, usable - first);
                kDrawPrims(context_, kPrimTriangles, kVertexFormatPositionColorUV, count, vertices + first);
            }
        }

        void DrawIndexedTriangles(const SC4WorldStyle& style, const SC4WorldVertex* vertices,
                                  const uint32_t vertexCount, const uint16_t* indices,
                                  const uint32_t indexCount) override {
            if (!vertices || !indices || indexCount < 3 || vertexCount == 0 || vertexCount > 65535 || !driver_) {
                return;
            }
            if (vertexCount > kMaxVerticesPerDraw) {
                // Too many vertices for one draw on the built-in driver. Draw plain triangles
                // instead, which can be split.
                scratch_.clear();
                scratch_.reserve(indexCount);
                for (uint32_t i = 0; i + 2 < indexCount; i += 3) {
                    if (indices[i] >= vertexCount || indices[i + 1] >= vertexCount || indices[i + 2] >= vertexCount) {
                        continue;
                    }
                    scratch_.push_back(vertices[indices[i]]);
                    scratch_.push_back(vertices[indices[i + 1]]);
                    scratch_.push_back(vertices[indices[i + 2]]);
                }
                DrawTriangles(style, scratch_.data(), static_cast<uint32_t>(scratch_.size()));
                return;
            }
            if (!ApplyStyle_(style)) {
                return;
            }
            // SC4DrawContext::DrawPrims only forwards to these two driver calls, so calling them
            // directly leaves the draw context's state cache untouched.
            driver_->InterleavedArrays(kVertexFormatPositionColorUV, 0, vertices);
            driver_->DrawElements(kPrimTriangles, static_cast<int32_t>(indexCount - indexCount % 3), kIndexTypeUInt16,
                                  indices);
        }

        void DrawLine(const SC4WorldStyle& style, const SC4WorldLineStyle& line, const SC4WorldVertex* points,
                      const uint32_t pointCount) override {
            scratch_.clear();
            if (line.unit == SC4WorldLineWidth::Meters) {
                SC4WorldMeshBuilder builder;
                builder.AddPolyline(points, pointCount, line.width, line.closed);
                DrawTriangles(style, builder.Data(), builder.Count());
                return;
            }
            AppendPixelLine(line, points, pointCount, scratch_);
            DrawTriangles(style, scratch_.data(), static_cast<uint32_t>(scratch_.size()));
        }

        bool WorldToScreen(const float x, const float y, const float z, float& screenX,
                           float& screenY) const override {
            cS3DVector3 screen{};
            if (!camera_ || !kCameraProject(camera_, cS3DVector3{x, y, z}, screen)) {
                return false;
            }
            screenX = screen.fX;
            screenY = screen.fY;
            return true;
        }

        [[nodiscard]] float MetersPerPixel(const float x, const float y, const float z) const override {
            cS3DVector3 screen{};
            cS3DVector3 shifted{};
            if (!camera_ || !kCameraProject(camera_, cS3DVector3{x, y, z}, screen) ||
                !kCameraUnProject(camera_, cS3DVector3{screen.fX + 1.0f, screen.fY, screen.fZ}, shifted)) {
                return 0.0f;
            }
            const float dx = shifted.fX - x;
            const float dy = shifted.fY - y;
            const float dz = shifted.fZ - z;
            return std::sqrt(dx * dx + dy * dy + dz * dz);
        }

        // Builds a camera-facing band of constant screen width: offsets each point sideways on
        // screen, then brings the offset back into the world at the point's own depth.
        void AppendPixelLine(const SC4WorldLineStyle& line, const SC4WorldVertex* points, const uint32_t count,
                             std::vector<SC4WorldVertex>& out) const {
            if (!camera_ || !points || count < 2 || line.width <= 0.0f) {
                return;
            }
            struct ScreenPoint {
                float x, y, z;
                bool valid;
            };
            std::vector<ScreenPoint> screen(count);
            for (uint32_t i = 0; i < count; ++i) {
                cS3DVector3 s{};
                const bool ok = kCameraProject(camera_, cS3DVector3{points[i].x, points[i].y, points[i].z}, s) &&
                                std::isfinite(s.fX) && std::isfinite(s.fY);
                screen[i] = {s.fX, s.fY, s.fZ, ok};
            }

            const bool closed = line.closed && count > 2;
            const float half = line.width * 0.5f;
            auto normal = [&](const uint32_t a, const uint32_t b, float& nx, float& ny) {
                const float dx = screen[b].x - screen[a].x;
                const float dy = screen[b].y - screen[a].y;
                const float len = std::sqrt(dx * dx + dy * dy);
                nx = len > 1e-4f ? -dy / len : 0.0f;
                ny = len > 1e-4f ? dx / len : 0.0f;
                return len > 1e-4f;
            };

            std::vector<SC4WorldVertex> left(count);
            std::vector<SC4WorldVertex> right(count);
            std::vector<uint8_t> placed(count, 0);
            for (uint32_t i = 0; i < count; ++i) {
                if (!screen[i].valid) {
                    continue;
                }
                const bool hasPrev = (closed || i > 0) && screen[(i + count - 1) % count].valid;
                const bool hasNext = (closed || i + 1 < count) && screen[(i + 1) % count].valid;
                float px = 0, py = 0, qx = 0, qy = 0;
                const bool prevOk = hasPrev && normal((i + count - 1) % count, i, px, py);
                const bool nextOk = hasNext && normal(i, (i + 1) % count, qx, qy);
                float nx = px + qx;
                float ny = py + qy;
                const float len = std::sqrt(nx * nx + ny * ny);
                if (len < 1e-4f) {
                    continue;
                }
                nx /= len;
                ny /= len;
                const float refX = nextOk ? qx : px;
                const float refY = nextOk ? qy : py;
                const float scale = (prevOk && nextOk) ? 1.0f / (std::max)(0.25f, nx * refX + ny * refY) : 1.0f;
                const float ox = nx * half * scale;
                const float oy = ny * half * scale;
                cS3DVector3 l{}, r{};
                if (!kCameraUnProject(camera_, cS3DVector3{screen[i].x + ox, screen[i].y + oy, screen[i].z}, l) ||
                    !kCameraUnProject(camera_, cS3DVector3{screen[i].x - ox, screen[i].y - oy, screen[i].z}, r)) {
                    continue;
                }
                left[i] = {l.fX, l.fY, l.fZ, points[i].color, 0, 0};
                right[i] = {r.fX, r.fY, r.fZ, points[i].color, 0, 0};
                placed[i] = 1;
            }

            const uint32_t segments = closed ? count : count - 1;
            for (uint32_t i = 0; i < segments; ++i) {
                const uint32_t j = (i + 1) % count;
                if (!placed[i] || !placed[j]) {
                    continue;
                }
                out.push_back(left[i]);
                out.push_back(left[j]);
                out.push_back(right[j]);
                out.push_back(left[i]);
                out.push_back(right[j]);
                out.push_back(right[i]);
            }
        }

    private:
        bool ApplyStyle_(const SC4WorldStyle& style) {
            uint32_t texture = 0;
            if (style.texture.id != 0) {
                texture = service_.ResolveTexture(style.texture);
                if (texture == 0) {
                    return false;
                }
            }

            switch (style.blend) {
            case SC4WorldBlend::Alpha:
                kEnableBlendStateFlag(context_, true);
                kSetBlendFunc(context_, kBlendSrcAlpha, kBlendInvSrcAlpha);
                break;
            case SC4WorldBlend::Additive:
                kEnableBlendStateFlag(context_, true);
                kSetBlendFunc(context_, kBlendSrcAlpha, kBlendOne);
                break;
            case SC4WorldBlend::Multiply:
                kEnableBlendStateFlag(context_, true);
                kSetBlendFunc(context_, kBlendDstColor, kBlendZero);
                break;
            case SC4WorldBlend::Premultiplied:
                kEnableBlendStateFlag(context_, true);
                kSetBlendFunc(context_, kBlendOne, kBlendInvSrcAlpha);
                break;
            case SC4WorldBlend::Opaque:
                kEnableBlendStateFlag(context_, false);
                break;
            }

            kEnableDepthTestFlag(context_, style.depth == SC4WorldDepth::HiddenBehindScene);
            kSetDepthFunc(context_, kDepthLessEqual);
            kEnableDepthMaskFlag(context_, style.writeDepth);
            kEnableCullFaceFlag(context_, style.cullBackFaces);
            kSetDepthOffset(context_, style.depthBias);
            kSetFog(context_, style.fadeWithFog && sceneFog_, nullptr, FLT_MAX, FLT_MAX);

            if (texture != 0) {
                kEnableTextureStateFlag(context_, true, 0);
                kSetTexture(context_, texture, 0);
                kSetTexEnvMode(context_, kTexEnvModulate, 0);
            }
            else {
                kEnableTextureStateFlag(context_, false, 0);
            }
            return true;
        }

        WorldDrawService& service_;
        DrawServicePass pass_;
        void* context_;
        cIGZGDriver* driver_;
        cS3DCamera* camera_;
        bool sceneFog_ = false;
        int previousDepthOffset_ = 0;
        std::vector<SC4WorldVertex> scratch_;
    };
}

WorldDrawService::WorldDrawService()
    : cRZBaseSystemService(kWorldDrawServiceID, 0) {}

uint32_t WorldDrawService::AddRef() {
    return cRZBaseSystemService::AddRef();
}

uint32_t WorldDrawService::Release() {
    return cRZBaseSystemService::Release();
}

bool WorldDrawService::QueryInterface(const uint32_t riid, void** ppvObj) {
    if (!ppvObj) {
        return false;
    }
    if (riid == GZIID_cIGZWorldDrawService) {
        *ppvObj = static_cast<cIGZWorldDrawService*>(this);
        AddRef();
        return true;
    }
    return cRZBaseSystemService::QueryInterface(riid, ppvObj);
}

uint32_t WorldDrawService::GetServiceID() const {
    return kWorldDrawServiceID;
}

void WorldDrawService::SetDependencies(DrawService* drawService, ImGuiService* imguiService) {
    drawService_ = drawService;
    imguiService_ = imguiService;
}

bool WorldDrawService::Init() {
    if (VersionDetection::GetInstance().GetGameVersion() != 641) {
        LOG_WARN("WorldDrawService: not available, it needs SimCity 4 version 641");
        return false;
    }
    if (!drawService_) {
        LOG_WARN("WorldDrawService: not available, it needs the draw service (EnableDrawService in "
                 "SC4RenderServices.ini)");
        return false;
    }
    initialized_ = true;
    return true;
}

bool WorldDrawService::Shutdown() {
    std::lock_guard lock(mutex_);
    if (drawService_) {
        for (uint32_t& token : passTokens_) {
            if (token != 0) {
                drawService_->UnregisterDrawPassCallback(token);
                token = 0;
            }
        }
    }
    layers_.clear();
    callbacks_.clear();
    initialized_ = false;
    return true;
}

bool WorldDrawService::IsValidPass_(const DrawServicePass pass) {
    return static_cast<uint8_t>(pass) <= static_cast<uint8_t>(DrawServicePass::PostDynamic);
}

bool WorldDrawService::EnsurePassHookLocked_(const DrawServicePass pass) {
    uint32_t& token = passTokens_[static_cast<size_t>(pass)];
    if (token != 0) {
        return true;
    }
    if (!initialized_ || !drawService_) {
        return false;
    }
    if (!drawService_->RegisterDrawPassCallback(pass, &WorldDrawService::PassCallback_, this, &token)) {
        LOG_WARN("WorldDrawService: could not hook draw pass {}, nothing will be drawn there",
                 static_cast<int>(pass));
        token = 0;
        return false;
    }
    return true;
}

WorldDrawService::Layer* WorldDrawService::FindLayerLocked_(const uint32_t layerId) {
    const auto it = std::find_if(layers_.begin(), layers_.end(),
                                 [layerId](const Layer& layer) { return layer.id == layerId; });
    return it == layers_.end() ? nullptr : &*it;
}

uint32_t WorldDrawService::CreateLayer(const DrawServicePass pass, const SC4WorldStyle& style, const int32_t order) {
    std::lock_guard lock(mutex_);
    if (!IsValidPass_(pass) || !EnsurePassHookLocked_(pass)) {
        return 0;
    }
    Layer layer;
    layer.id = nextLayerId_++;
    layer.pass = pass;
    layer.order = order;
    layer.sequence = nextSequence_++;
    layer.style = style;
    layers_.push_back(std::move(layer));
    std::stable_sort(layers_.begin(), layers_.end(), [](const Layer& a, const Layer& b) {
        return a.order != b.order ? a.order < b.order : a.sequence < b.sequence;
    });
    return nextLayerId_ - 1;
}

void WorldDrawService::DestroyLayer(const uint32_t layerId) {
    std::lock_guard lock(mutex_);
    std::erase_if(layers_, [layerId](const Layer& layer) { return layer.id == layerId; });
}

bool WorldDrawService::ClearLayer(const uint32_t layerId) {
    std::lock_guard lock(mutex_);
    Layer* layer = FindLayerLocked_(layerId);
    if (!layer) {
        return false;
    }
    layer->triangles.clear();
    layer->pixelLines.clear();
    return true;
}

bool WorldDrawService::AddLayerTriangles(const uint32_t layerId, const SC4WorldVertex* vertices,
                                         const uint32_t vertexCount) {
    std::lock_guard lock(mutex_);
    Layer* layer = FindLayerLocked_(layerId);
    if (!layer || (!vertices && vertexCount > 0)) {
        return false;
    }
    layer->triangles.insert(layer->triangles.end(), vertices, vertices + (vertexCount - vertexCount % 3));
    return true;
}

bool WorldDrawService::AddLayerLine(const uint32_t layerId, const SC4WorldLineStyle& line,
                                    const SC4WorldVertex* points, const uint32_t pointCount) {
    std::lock_guard lock(mutex_);
    Layer* layer = FindLayerLocked_(layerId);
    if (!layer || !points || pointCount < 2) {
        return false;
    }
    if (line.unit == SC4WorldLineWidth::Meters) {
        // A metre-wide band does not depend on the camera, so build it once.
        SC4WorldMeshBuilder builder;
        builder.AddPolyline(points, pointCount, line.width, line.closed);
        layer->triangles.insert(layer->triangles.end(), builder.Vertices().begin(), builder.Vertices().end());
        return true;
    }
    layer->pixelLines.push_back({line, std::vector<SC4WorldVertex>(points, points + pointCount)});
    return true;
}

bool WorldDrawService::SetLayerTriangles(const uint32_t layerId, const SC4WorldVertex* vertices,
                                         const uint32_t vertexCount) {
    std::lock_guard lock(mutex_);
    Layer* layer = FindLayerLocked_(layerId);
    if (!layer || (!vertices && vertexCount > 0)) {
        return false;
    }
    layer->pixelLines.clear();
    layer->triangles.assign(vertices, vertices + (vertexCount - vertexCount % 3));
    return true;
}

bool WorldDrawService::SetLayerVisible(const uint32_t layerId, const bool visible) {
    std::lock_guard lock(mutex_);
    Layer* layer = FindLayerLocked_(layerId);
    if (!layer) {
        return false;
    }
    layer->visible = visible;
    return true;
}

bool WorldDrawService::SetLayerStyle(const uint32_t layerId, const SC4WorldStyle& style) {
    std::lock_guard lock(mutex_);
    Layer* layer = FindLayerLocked_(layerId);
    if (!layer) {
        return false;
    }
    layer->style = style;
    return true;
}

bool WorldDrawService::RegisterDrawCallback(const DrawServicePass pass, const SC4WorldDrawCallback callback,
                                            void* userData, uint32_t* outToken) {
    std::lock_guard lock(mutex_);
    if (!callback || !IsValidPass_(pass) || !EnsurePassHookLocked_(pass)) {
        return false;
    }
    const uint32_t token = nextToken_++;
    callbacks_.push_back({token, pass, callback, userData});
    if (outToken) {
        *outToken = token;
    }
    return true;
}

void WorldDrawService::UnregisterDrawCallback(const uint32_t token) {
    std::lock_guard lock(mutex_);
    std::erase_if(callbacks_, [token](const CallbackEntry& entry) { return entry.token == token; });
}

bool WorldDrawService::PickTerrain(const int32_t screenX, const int32_t screenY, float& worldX, float& worldY,
                                   float& worldZ) {
    const auto view3DWin = SC4UI::GetView3DWin();
    if (!view3DWin) {
        return false;
    }
    float world[3] = {};
    if (!view3DWin->PickTerrain(screenX, screenY, world, false)) {
        return false;
    }
    // The game reports some misses as success with an empty or broken point.
    if (!std::isfinite(world[0]) || !std::isfinite(world[1]) || !std::isfinite(world[2]) ||
        (world[0] == 0.0f && world[1] == 0.0f && world[2] == 0.0f)) {
        return false;
    }
    worldX = world[0];
    worldY = world[1];
    worldZ = world[2];
    return true;
}

bool WorldDrawService::GetTerrainHeight(const float x, const float z, float& height) {
    const cISC4AppPtr app;
    cISC4City* city = app ? app->GetCity() : nullptr;
    cISTETerrain* terrain = city ? city->GetTerrain() : nullptr;
    if (!terrain || !terrain->LocationIsInBounds(x, z)) {
        return false;
    }
    height = terrain->GetAltitude(x, z);
    return std::isfinite(height);
}

uint32_t WorldDrawService::ResolveTexture(const ImGuiTextureHandle& handle) {
    void* id = imguiService_ ? imguiService_->GetTextureID(handle) : nullptr;
    const auto texture = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(id));
    if (texture == 0 && warnedTextures_.insert(handle.id).second) {
        LOG_WARN("WorldDrawService: texture {} is not available, so the shapes using it are not drawn. It may have "
                 "been released, or the graphics device was reset and the plugin has not recreated it.", handle.id);
    }
    return texture;
}

void WorldDrawService::PassCallback_(const DrawServicePass pass, const bool begin, void* userData) {
    if (!begin && userData) {
        static_cast<WorldDrawService*>(userData)->DrawPass_(pass);
    }
}

void WorldDrawService::DrawPass_(const DrawServicePass pass) {
    std::vector<CallbackEntry> callbacks;
    bool hasLayers = false;
    {
        std::lock_guard lock(mutex_);
        for (const auto& layer : layers_) {
            if (layer.pass == pass && layer.visible && (!layer.triangles.empty() || !layer.pixelLines.empty())) {
                hasLayers = true;
                break;
            }
        }
        for (const auto& entry : callbacks_) {
            if (entry.pass == pass) {
                callbacks.push_back(entry);
            }
        }
    }
    if (!hasLayers && callbacks.empty()) {
        return;
    }

    void* renderer = ActiveRenderer();
    void* context = renderer ? kGetDrawContext(renderer) : nullptr;
    if (!context || !Field<cIGZGDriver*>(context, kContextDriverOffset)) {
        // Normal outside city view, for example in the region view.
        if (!warnedNoContext_) {
            LOG_DEBUG("WorldDrawService: no city view to draw into yet, world drawing starts once a city is open");
            warnedNoContext_ = true;
        }
        return;
    }
    cS3DCamera* camera = static_cast<cISC43DRender*>(renderer)->GetCamera();

    WorldFrame frame(*this, pass, context, camera);
    frame.Begin();
    if (hasLayers) {
        std::lock_guard lock(mutex_);
        std::vector<SC4WorldVertex> lineScratch;
        for (const auto& layer : layers_) {
            if (layer.pass != pass || !layer.visible) {
                continue;
            }
            frame.DrawTriangles(layer.style, layer.triangles.data(), static_cast<uint32_t>(layer.triangles.size()));
            if (!layer.pixelLines.empty()) {
                lineScratch.clear();
                for (const auto& line : layer.pixelLines) {
                    frame.AppendPixelLine(line.style, line.points.data(), static_cast<uint32_t>(line.points.size()),
                                          lineScratch);
                }
                frame.DrawTriangles(layer.style, lineScratch.data(), static_cast<uint32_t>(lineScratch.size()));
            }
        }
    }
    for (const auto& entry : callbacks) {
        entry.callback(frame, entry.userData);
    }
    frame.End();
}
