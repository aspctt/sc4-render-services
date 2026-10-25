#pragma once

#include <cstdint>

#include "cIGZUnknown.h"
#include "cIGZDrawService.h"
#include "cIGZImGuiService.h"

// Unique IDs for the world drawing service and its interface.
static constexpr uint32_t kWorldDrawServiceID = 0x5D1C7A43;
static constexpr uint32_t GZIID_cIGZWorldDrawService = 0x8E2B4F16;

/// A point in the city, in metres, in the game's own coordinates: x and z run along the ground,
/// y is the height. A city tile is 16 by 16 metres.
struct SC4WorldVertex
{
    float x;
    float y;
    float z;
    /// Colour as 0xAARRGGBB. Use SC4WorldColor to build one.
    uint32_t color;
    /// Texture coordinates. Ignored when the style has no texture.
    float u;
    float v;
};
static_assert(sizeof(SC4WorldVertex) == 24, "SC4WorldVertex matches the driver's position, colour, UV format");

/// Packs a colour from 0 to 1 floats into 0xAARRGGBB.
constexpr uint32_t SC4WorldColor(const float r, const float g, const float b, const float a = 1.0f)
{
    auto channel = [](const float value) -> uint32_t {
        const float clamped = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
        return static_cast<uint32_t>(clamped * 255.0f + 0.5f);
    };
    return (channel(a) << 24) | (channel(r) << 16) | (channel(g) << 8) | channel(b);
}

/// Packs a colour from 0 to 255 channels into 0xAARRGGBB.
constexpr uint32_t SC4WorldColorRGBA(const uint8_t r, const uint8_t g, const uint8_t b, const uint8_t a = 255)
{
    return (static_cast<uint32_t>(a) << 24) | (static_cast<uint32_t>(r) << 16) |
           (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b);
}

/// How drawn colours combine with the scene behind them.
enum class SC4WorldBlend : uint8_t
{
    Alpha,          ///< Normal transparency by the colour's alpha.
    Additive,       ///< Adds light, for glows and highlights. Alpha scales the added colour.
    Multiply,       ///< Darkens the scene by the colour, for shading. Alpha is ignored.
    Premultiplied,  ///< Colours already multiplied by their alpha.
    Opaque,         ///< No transparency.
};

/// Whether the scene can hide what you draw.
enum class SC4WorldDepth : uint8_t
{
    HiddenBehindScene,  ///< Hills, buildings and other scenery in front cover it.
    AlwaysOnTop,        ///< Always visible, drawn over everything.
};

/// How a batch of triangles or lines is drawn. Every field has a sensible default, so set only
/// what you need, for example SC4WorldStyle{.depth = SC4WorldDepth::AlwaysOnTop}.
struct SC4WorldStyle
{
    SC4WorldBlend blend = SC4WorldBlend::Alpha;
    SC4WorldDepth depth = SC4WorldDepth::HiddenBehindScene;
    /// Pulls the drawing slightly towards the camera so shapes lying on the ground do not
    /// flicker against it. 0 is none. The game's own decals use 2.
    int8_t depthBias = 1;
    /// Writes depth so later scenery is hidden behind this. Only useful for solid shapes.
    bool writeDepth = false;
    /// Fades into the distance with the game's fog, like the rest of the scene. Has no effect
    /// when the player has fog turned off.
    bool fadeWithFog = false;
    /// Skips triangles facing away from the camera. Only useful for closed solid shapes.
    bool cullBackFaces = false;
    /// A texture from cIGZImGuiService::CreateTexture, multiplied by the vertex colour.
    /// Leave it empty to draw plain colours.
    ImGuiTextureHandle texture{};
};

/// Whether a line width is measured in the world or on screen.
enum class SC4WorldLineWidth : uint8_t
{
    Pixels,  ///< Keeps the same thickness on screen at every zoom level. Faces the camera.
    Meters,  ///< Has a fixed width in the world and lies flat, like a road marking.
};

/// How a line through a list of points is drawn.
struct SC4WorldLineStyle
{
    float width = 2.0f;
    SC4WorldLineWidth unit = SC4WorldLineWidth::Pixels;
    /// Joins the last point back to the first.
    bool closed = false;
};

// ReSharper disable once CppPolymorphicClassWithNonVirtualPublicDestructor
/// Draws during a world draw callback. Only valid inside that callback.
class cIGZWorldDrawContext
{
public:
    /// The draw pass this callback runs in.
    [[nodiscard]] virtual DrawServicePass GetPass() const = 0;

    /// Draws a list of triangles, three vertices each.
    virtual void DrawTriangles(const SC4WorldStyle& style, const SC4WorldVertex* vertices, uint32_t vertexCount) = 0;

    /// Draws triangles from shared vertices, three indices each. At most 65535 vertices.
    virtual void DrawIndexedTriangles(const SC4WorldStyle& style, const SC4WorldVertex* vertices,
                                      uint32_t vertexCount, const uint16_t* indices, uint32_t indexCount) = 0;

    /// Draws a line through the points, using each point's colour. UVs are ignored.
    virtual void DrawLine(const SC4WorldStyle& style, const SC4WorldLineStyle& line,
                          const SC4WorldVertex* points, uint32_t pointCount) = 0;

    /// Projects a world point onto the screen, in game window pixels. Returns false when the
    /// point is behind the camera.
    virtual bool WorldToScreen(float x, float y, float z, float& screenX, float& screenY) const = 0;

    /// How many metres one screen pixel covers at a world point. Use it to size markers so
    /// they look the same at every zoom level.
    [[nodiscard]] virtual float MetersPerPixel(float x, float y, float z) const = 0;
};

/// Called at the end of a draw pass, every frame the city is drawn.
using SC4WorldDrawCallback = void (*)(cIGZWorldDrawContext& context, void* userData);

// ReSharper disable once CppPolymorphicClassWithNonVirtualPublicDestructor
/// Draws shapes, lines and textures into the city through the game's own graphics driver, so
/// they work with the built-in DirectX driver, SCD3D11 and scvk alike.
///
/// There are two ways to draw:
/// - Layers hold geometry inside SC4RenderServices and are drawn every frame without any work
///   from your plugin. Fill a layer when your data changes. This is the right choice for
///   almost everything: grids, footprints, markings, area overlays.
/// - Draw callbacks run every frame, for things that really change every frame, such as a
///   preview following the mouse.
///
/// Passes decide what your drawing can be hidden behind:
/// - PreDynamic: after buildings, terrain and other static scenery, before cars and other
///   moving things. Good for things on the ground.
/// - PostDynamic: after everything. Good for things that should sit on top.
///
/// Call everything from the game's main thread.
class cIGZWorldDrawService : public cIGZUnknown
{
public:
    /// Returns the service ID (kWorldDrawServiceID).
    [[nodiscard]] virtual uint32_t GetServiceID() const = 0;

    /** @name Layers */
    ///@{
    /// Creates an empty, visible layer drawn at the end of the given pass. Layers in the same
    /// pass draw in ascending order. Returns 0 on failure.
    virtual uint32_t CreateLayer(DrawServicePass pass, const SC4WorldStyle& style, int32_t order) = 0;
    /// Removes a layer and its contents.
    virtual void DestroyLayer(uint32_t layerId) = 0;
    /// Removes everything from a layer.
    virtual bool ClearLayer(uint32_t layerId) = 0;
    /// Adds triangles to a layer, three vertices each. The data is copied.
    virtual bool AddLayerTriangles(uint32_t layerId, const SC4WorldVertex* vertices, uint32_t vertexCount) = 0;
    /// Adds a line to a layer. The points are copied. Pixel-wide lines keep their width while
    /// the player zooms.
    virtual bool AddLayerLine(uint32_t layerId, const SC4WorldLineStyle& line,
                              const SC4WorldVertex* points, uint32_t pointCount) = 0;
    /// Replaces a layer's contents with these triangles. Same as ClearLayer then AddLayerTriangles.
    virtual bool SetLayerTriangles(uint32_t layerId, const SC4WorldVertex* vertices, uint32_t vertexCount) = 0;
    virtual bool SetLayerVisible(uint32_t layerId, bool visible) = 0;
    virtual bool SetLayerStyle(uint32_t layerId, const SC4WorldStyle& style) = 0;
    ///@}

    /** @name Draw callbacks */
    ///@{
    /// Registers a callback that draws at the end of the given pass, after that pass's layers.
    virtual bool RegisterDrawCallback(DrawServicePass pass, SC4WorldDrawCallback callback, void* userData,
                                      uint32_t* outToken) = 0;
    virtual void UnregisterDrawCallback(uint32_t token) = 0;
    ///@}

    /** @name Terrain */
    ///@{
    /// Finds the terrain point under a screen position in game window pixels, for example
    /// ImGui's mouse position. Returns false when no city is loaded or nothing was hit.
    virtual bool PickTerrain(int32_t screenX, int32_t screenY, float& worldX, float& worldY, float& worldZ) = 0;
    /// The terrain height at a point. Returns false when no city is loaded or the point is
    /// outside the city.
    virtual bool GetTerrainHeight(float x, float z, float& height) = 0;
    ///@}
};
