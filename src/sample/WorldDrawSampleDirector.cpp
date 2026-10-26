// Shows the world draw service: layers that are filled once and then drawn by
// SC4RenderServices every frame, a draw callback for things that follow the mouse, lines that
// keep their width on screen, and an ImGui texture laid on the ground. Everything goes through
// the game's own graphics driver, so it looks the same with DirectX, SCD3D11 and scvk.
#include "cIGZFrameWork.h"
#include "cRZCOMDllDirector.h"

#include "imgui.h"
#include "public/ImGuiPanelAdapter.h"
#include "public/ImGuiServiceIds.h"
#include "public/ImGuiTexture.h"
#include "public/SC4WorldMeshBuilder.h"
#include "public/cIGZImGuiService.h"
#include "public/cIGZWorldDrawService.h"
#include "utils/Logger.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace
{
    constexpr uint32_t kWorldDrawSampleDirectorID = 0x7B21C4E5;
    constexpr uint32_t kWorldDrawPanelId = 0x7B21C4E6;
    constexpr float kTile = SC4WorldMeshBuilder::kTileSize;

    class WorldDrawSamplePanel final : public ImGuiPanel
    {
    public:
        WorldDrawSamplePanel(cIGZImGuiService* imgui, cIGZWorldDrawService* world)
            : imgui_(imgui)
            , world_(world) {}

        void OnInit() override
        {
            // Layers in PreDynamic sit on the ground: buildings and hills in front hide them.
            grid_ = world_->CreateLayer(DrawServicePass::PreDynamic, GridStyle_(), 0);
            shapes_ = world_->CreateLayer(DrawServicePass::PreDynamic, SC4WorldStyle{.fadeWithFog = true}, 1);
            picture_ = world_->CreateLayer(DrawServicePass::PreDynamic, SC4WorldStyle{}, 2);
            // A route drawn on top of everything, with lines that keep their width on screen.
            route_ = world_->CreateLayer(DrawServicePass::PostDynamic,
                                         SC4WorldStyle{.depth = SC4WorldDepth::AlwaysOnTop}, 0);
            world_->RegisterDrawCallback(DrawServicePass::PostDynamic, &DrawCursor_, this, &cursorToken_);
            CreateCheckerTexture_();
        }

        void OnShutdown() override { Cleanup_(); }
        void OnUnregister() override { Cleanup_(); }

        void OnRender() override
        {
            TrackMouse_();
            KeepTextureAlive_();

            ImGui::SetNextWindowSize(ImVec2(360, 0), ImGuiCond_FirstUseEver);
            if (!ImGui::Begin("World drawing")) {
                ImGui::End();
                return;
            }

            SC4GraphicsDriverInfo driver{};
            if (imgui_->GetActiveDriverInfo(driver)) {
                ImGui::TextWrapped("Drawing through %s", driver.name);
            }

            if (ImGui::Button("Place the showcase in the middle of the screen")) {
                const ImVec2 size = ImGui::GetIO().DisplaySize;
                float x, y, z;
                if (world_->PickTerrain(static_cast<int32_t>(size.x * 0.5f), static_cast<int32_t>(size.y * 0.5f),
                                        x, y, z)) {
                    origin_ = {std::floor(x / kTile) * kTile, std::floor(z / kTile) * kTile};
                    placed_ = true;
                    RebuildAll_();
                }
            }
            if (!placed_) {
                ImGui::TextWrapped("Open a city, then place the showcase.");
            }

            ImGui::SeparatorText("Layers");
            LayerToggle_("Tile grid", grid_, showGrid_);
            ImGui::SameLine();
            if (ImGui::Checkbox("Hidden behind hills", &gridHidden_)) {
                world_->SetLayerStyle(grid_, GridStyle_());
            }
            LayerToggle_("Shapes", shapes_, showShapes_);
            LayerToggle_("Picture on the ground", picture_, showPicture_);
            LayerToggle_("Route on top", route_, showRoute_);
            if (ImGui::SliderFloat("Route width", &routeWidth_, 1.0f, 12.0f, "%.0f px") && placed_) {
                RebuildRoute_();
            }

            ImGui::SeparatorText("Cursor");
            ImGui::Checkbox("Glow under the mouse", &showCursor_);
            ImGui::SameLine();
            ImGui::Checkbox("Additive", &cursorAdditive_);
            if (hasMouse_) {
                ImGui::Text("Terrain under the mouse: %.1f, %.1f, %.1f", mouse_[0], mouse_[1], mouse_[2]);
            }
            ImGui::End();
        }

    private:
        void Cleanup_()
        {
            if (cursorToken_ != 0) {
                world_->UnregisterDrawCallback(cursorToken_);
                cursorToken_ = 0;
            }
            for (uint32_t* layer : {&grid_, &shapes_, &picture_, &route_}) {
                if (*layer != 0) {
                    world_->DestroyLayer(*layer);
                    *layer = 0;
                }
            }
            checker_.Release();
        }

        SC4WorldStyle GridStyle_() const
        {
            return SC4WorldStyle{.depth = gridHidden_ ? SC4WorldDepth::HiddenBehindScene : SC4WorldDepth::AlwaysOnTop};
        }

        void LayerToggle_(const char* label, const uint32_t layer, bool& visible)
        {
            if (ImGui::Checkbox(label, &visible)) {
                world_->SetLayerVisible(layer, visible);
            }
        }

        // ImGui's mouse position is in game window pixels, the same as PickTerrain wants.
        void TrackMouse_()
        {
            const ImGuiIO& io = ImGui::GetIO();
            hasMouse_ = !io.WantCaptureMouse && ImGui::IsMousePosValid() &&
                        world_->PickTerrain(static_cast<int32_t>(io.MousePos.x), static_cast<int32_t>(io.MousePos.y),
                                            mouse_[0], mouse_[1], mouse_[2]);
        }

        void RebuildAll_()
        {
            SC4WorldMeshBuilder mesh;
            mesh.DrapeOnTerrain(world_);

            // An 8 by 8 tile grid with a highlighted tile in the middle.
            mesh.AddGroundGrid({origin_.x - 4 * kTile, origin_.z - 4 * kTile},
                               {origin_.x + 4 * kTile, origin_.z + 4 * kTile}, kTile, 0.4f,
                               SC4WorldColor(1, 1, 1, 0.45f));
            mesh.AddGroundTile(static_cast<int32_t>(origin_.x / kTile), static_cast<int32_t>(origin_.z / kTile),
                               SC4WorldColor(0.2f, 0.8f, 1.0f, 0.35f), 1.0f);
            world_->SetLayerTriangles(grid_, mesh.Data(), mesh.Count());

            // A concave area with its outline, a ring, a rotated rectangle and three posts.
            mesh.Clear();
            const SC4WorldPoint2 area[] = {
                {origin_.x - 56, origin_.z - 56}, {origin_.x - 8, origin_.z - 56}, {origin_.x - 8, origin_.z - 32},
                {origin_.x - 32, origin_.z - 32}, {origin_.x - 32, origin_.z - 8}, {origin_.x - 56, origin_.z - 8},
            };
            mesh.AddGroundPolygon(area, 6, SC4WorldColor(0.2f, 0.4f, 1.0f, 0.35f));
            mesh.AddGroundPolyline(area, 6, 0.8f, SC4WorldColor(0.4f, 0.6f, 1.0f, 0.9f), true);
            mesh.AddGroundRing({origin_.x + 40, origin_.z - 40}, 14.0f, 1.5f, SC4WorldColor(1, 0.6f, 0.1f, 0.9f));
            mesh.AddGroundRectOutline({origin_.x + 40, origin_.z + 40}, 24.0f, 12.0f, 0.6f, 1.0f,
                                      SC4WorldColor(1, 1, 0.2f, 0.9f));
            for (int i = 0; i < 3; ++i) {
                mesh.AddMarker({origin_.x - 8 + 8.0f * static_cast<float>(i), origin_.z + 24}, 12.0f, 0.8f,
                               SC4WorldColor(1, 0.25f, 0.25f, 0.9f));
            }
            world_->SetLayerTriangles(shapes_, mesh.Data(), mesh.Count());

            RebuildPicture_();
            RebuildRoute_();
        }

        void RebuildPicture_()
        {
            if (!placed_) {
                return;
            }
            SC4WorldMeshBuilder mesh;
            mesh.DrapeOnTerrain(world_);
            mesh.AddGroundRect({origin_.x + 40, origin_.z}, 32.0f, 32.0f, 0.0f, SC4WorldColor(1, 1, 1, 0.85f));
            world_->SetLayerTriangles(picture_, mesh.Data(), mesh.Count());
        }

        // A loop around the showcase, a few metres above the ground, fading from green to red.
        void RebuildRoute_()
        {
            constexpr int kPoints = 48;
            std::vector<SC4WorldVertex> points;
            for (int i = 0; i < kPoints; ++i) {
                const float t = static_cast<float>(i) / kPoints;
                const float angle = t * 6.2831853f;
                const float radius = 70.0f + 10.0f * std::sin(angle * 3.0f);
                const float x = origin_.x + std::cos(angle) * radius;
                const float z = origin_.z + std::sin(angle) * radius;
                float ground = 0.0f;
                world_->GetTerrainHeight(x, z, ground);
                const float red = std::fabs(2.0f * t - 1.0f);
                points.push_back({x, ground + 3.0f, z, SC4WorldColor(red, 1.0f - red, 0.2f), 0, 0});
            }
            world_->ClearLayer(route_);
            world_->AddLayerLine(route_, SC4WorldLineStyle{.width = routeWidth_, .closed = true}, points.data(),
                                 static_cast<uint32_t>(points.size()));
        }

        void CreateCheckerTexture_()
        {
            constexpr uint32_t kSize = 64;
            std::vector<uint8_t> pixels(kSize * kSize * 4);
            for (uint32_t y = 0; y < kSize; ++y) {
                for (uint32_t x = 0; x < kSize; ++x) {
                    const bool light = ((x / 8) + (y / 8)) % 2 == 0;
                    uint8_t* p = &pixels[(y * kSize + x) * 4];
                    p[0] = light ? 250 : 40;
                    p[1] = light ? 200 : 90;
                    p[2] = light ? 60 : 160;
                    p[3] = 255;
                }
            }
            checker_.Create(imgui_, kSize, kSize, pixels.data());
            world_->SetLayerStyle(picture_, SC4WorldStyle{.texture = checker_.GetHandle()});
        }

        // Textures are lost when the graphics device resets. Make a new one and hand the layer
        // the new handle.
        void KeepTextureAlive_()
        {
            if (imgui_->IsDeviceReady() && !checker_.GetID()) {
                CreateCheckerTexture_();
            }
        }

        // Runs every frame in PostDynamic: a soft glow that stays the same size on screen.
        static void DrawCursor_(cIGZWorldDrawContext& context, void* userData)
        {
            auto* self = static_cast<WorldDrawSamplePanel*>(userData);
            if (!self->showCursor_ || !self->hasMouse_) {
                return;
            }
            const float x = self->mouse_[0];
            const float y = self->mouse_[1];
            const float z = self->mouse_[2];
            const float radius = 28.0f * context.MetersPerPixel(x, y, z);
            if (radius <= 0.0f) {
                return;
            }

            SC4WorldMeshBuilder mesh;
            mesh.DrapeOnTerrain(self->world_, 0.1f);
            mesh.SetMaxEdgeLength((std::max)(radius / 4.0f, 0.5f));
            mesh.AddGroundCircle({x, z}, radius, SC4WorldColor(0.3f, 0.8f, 1.0f, 0.35f));
            const SC4WorldStyle glow{
                .blend = self->cursorAdditive_ ? SC4WorldBlend::Additive : SC4WorldBlend::Alpha,
                .depth = SC4WorldDepth::AlwaysOnTop,
            };
            context.DrawTriangles(glow, mesh.Data(), mesh.Count());

            std::array<SC4WorldVertex, 32> ring{};
            for (size_t i = 0; i < ring.size(); ++i) {
                const float angle = 6.2831853f * static_cast<float>(i) / static_cast<float>(ring.size());
                const float px = x + std::cos(angle) * radius;
                const float pz = z + std::sin(angle) * radius;
                ring[i] = {px, mesh.GroundHeight(px, pz), pz, SC4WorldColor(1, 1, 1, 0.9f), 0, 0};
            }
            context.DrawLine(SC4WorldStyle{.depth = SC4WorldDepth::AlwaysOnTop},
                             SC4WorldLineStyle{.width = 2.0f, .closed = true}, ring.data(),
                             static_cast<uint32_t>(ring.size()));
        }

        cIGZImGuiService* imgui_;
        cIGZWorldDrawService* world_;
        ImGuiTexture checker_;
        uint32_t grid_ = 0;
        uint32_t shapes_ = 0;
        uint32_t picture_ = 0;
        uint32_t route_ = 0;
        uint32_t cursorToken_ = 0;
        SC4WorldPoint2 origin_{};
        bool placed_ = false;
        bool showGrid_ = true;
        bool gridHidden_ = true;
        bool showShapes_ = true;
        bool showPicture_ = true;
        bool showRoute_ = true;
        bool showCursor_ = true;
        bool cursorAdditive_ = false;
        float routeWidth_ = 3.0f;
        float mouse_[3] = {};
        bool hasMouse_ = false;
    };
}

class WorldDrawSampleDirector final : public cRZCOMDllDirector
{
public:
    [[nodiscard]] uint32_t GetDirectorID() const override
    {
        return kWorldDrawSampleDirectorID;
    }

    bool OnStart(cIGZCOM* pCOM) override
    {
        cRZCOMDllDirector::OnStart(pCOM);
        Logger::Initialize("SC4WorldDrawSample", "");
        if (mpFrameWork) {
            mpFrameWork->AddHook(this);
        }
        return true;
    }

    bool PostAppInit() override
    {
        if (!mpFrameWork) {
            return true;
        }
        if (!mpFrameWork->GetSystemService(kImGuiServiceID, GZIID_cIGZImGuiService,
                                           reinterpret_cast<void**>(&imgui_))) {
            LOG_WARN("WorldDrawSample: the ImGui service is not available");
            return true;
        }
        if (!mpFrameWork->GetSystemService(kWorldDrawServiceID, GZIID_cIGZWorldDrawService,
                                           reinterpret_cast<void**>(&world_))) {
            LOG_WARN("WorldDrawSample: the world draw service is not available. Update SC4RenderServices.dll.");
            return true;
        }

        auto* panel = new WorldDrawSamplePanel(imgui_, world_);
        const ImGuiPanelDesc desc = ImGuiPanelAdapter<WorldDrawSamplePanel>::MakeDesc(panel, kWorldDrawPanelId, 130, true);
        if (!imgui_->RegisterPanel(desc)) {
            delete panel;
            return true;
        }
        panelRegistered_ = true;
        return true;
    }

    bool PostAppShutdown() override
    {
        if (imgui_ && panelRegistered_) {
            imgui_->UnregisterPanel(kWorldDrawPanelId);
            panelRegistered_ = false;
        }
        if (world_) {
            world_->Release();
            world_ = nullptr;
        }
        if (imgui_) {
            imgui_->Release();
            imgui_ = nullptr;
        }
        return true;
    }

private:
    cIGZImGuiService* imgui_ = nullptr;
    cIGZWorldDrawService* world_ = nullptr;
    bool panelRegistered_ = false;
};

static WorldDrawSampleDirector sDirector;

cRZCOMDllDirector* RZGetCOMDllDirector()
{
    static bool sAddedRef = false;
    if (!sAddedRef) {
        sDirector.AddRef();
        sAddedRef = true;
    }
    return &sDirector;
}
