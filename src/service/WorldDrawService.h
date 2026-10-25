#pragma once

#include <array>
#include <cstdint>
#include <mutex>
#include <unordered_set>
#include <vector>

#include "cRZBaseSystemService.h"
#include "public/cIGZWorldDrawService.h"

class DrawService;
class ImGuiService;

// ReSharper disable once CppPolymorphicClassWithNonVirtualPublicDestructor
class WorldDrawService final : public cRZBaseSystemService, public cIGZWorldDrawService
{
public:
    WorldDrawService();
    ~WorldDrawService() = default;

    uint32_t AddRef() override;
    uint32_t Release() override;
    bool QueryInterface(uint32_t riid, void** ppvObj) override;

    [[nodiscard]] uint32_t GetServiceID() const override;

    uint32_t CreateLayer(DrawServicePass pass, const SC4WorldStyle& style, int32_t order) override;
    void DestroyLayer(uint32_t layerId) override;
    bool ClearLayer(uint32_t layerId) override;
    bool AddLayerTriangles(uint32_t layerId, const SC4WorldVertex* vertices, uint32_t vertexCount) override;
    bool AddLayerLine(uint32_t layerId, const SC4WorldLineStyle& line, const SC4WorldVertex* points,
                      uint32_t pointCount) override;
    bool SetLayerTriangles(uint32_t layerId, const SC4WorldVertex* vertices, uint32_t vertexCount) override;
    bool SetLayerVisible(uint32_t layerId, bool visible) override;
    bool SetLayerStyle(uint32_t layerId, const SC4WorldStyle& style) override;

    bool RegisterDrawCallback(DrawServicePass pass, SC4WorldDrawCallback callback, void* userData,
                              uint32_t* outToken) override;
    void UnregisterDrawCallback(uint32_t token) override;

    bool PickTerrain(int32_t screenX, int32_t screenY, float& worldX, float& worldY, float& worldZ) override;
    bool GetTerrainHeight(float x, float z, float& height) override;

    // Draws through the draw service's pass hooks. imguiService may be null, then textured
    // styles are skipped. Call before Init.
    void SetDependencies(DrawService* drawService, ImGuiService* imguiService);
    bool Init() override;
    bool Shutdown() override;

    // Turns an ImGui texture handle into a driver texture name, 0 when it is not available.
    // Logs once per texture that cannot be used.
    uint32_t ResolveTexture(const ImGuiTextureHandle& handle);

private:
    struct PixelLine
    {
        SC4WorldLineStyle style;
        std::vector<SC4WorldVertex> points;
    };

    struct Layer
    {
        uint32_t id = 0;
        DrawServicePass pass = DrawServicePass::PreDynamic;
        int32_t order = 0;
        uint32_t sequence = 0;  // keeps creation order between layers with the same order
        SC4WorldStyle style{};
        bool visible = true;
        std::vector<SC4WorldVertex> triangles;  // metre-wide lines are stored here too
        std::vector<PixelLine> pixelLines;      // rebuilt from the camera every frame
    };

    struct CallbackEntry
    {
        uint32_t token = 0;
        DrawServicePass pass = DrawServicePass::PreDynamic;
        SC4WorldDrawCallback callback = nullptr;
        void* userData = nullptr;
    };

    static void PassCallback_(DrawServicePass pass, bool begin, void* userData);
    void DrawPass_(DrawServicePass pass);
    bool EnsurePassHookLocked_(DrawServicePass pass);
    Layer* FindLayerLocked_(uint32_t layerId);
    static bool IsValidPass_(DrawServicePass pass);

    DrawService* drawService_ = nullptr;
    ImGuiService* imguiService_ = nullptr;
    std::vector<Layer> layers_;
    std::vector<CallbackEntry> callbacks_;
    std::array<uint32_t, 6> passTokens_{};
    std::unordered_set<uint32_t> warnedTextures_;
    std::mutex mutex_;
    uint32_t nextLayerId_ = 1;
    uint32_t nextToken_ = 1;
    uint32_t nextSequence_ = 1;
    bool initialized_ = false;
    bool warnedNoContext_ = false;
};
