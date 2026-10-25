#pragma once

#include <cstdint>

/// The graphics driver the game draws with.
/// The game's class ID does not tell these apart: SCD3D11 reports the DirectX class ID like the
/// built-in driver, and scvk reports the OpenGL one. They are identified by the module that
/// implements the driver instead.
enum class SC4GraphicsDriverKind : uint32_t
{
    Unknown = 0,         ///< A driver from a module SC4RenderServices does not know.
    BuiltInDirectX = 1,  ///< The game's own DirectX 7 driver.
    BuiltInOpenGL = 2,   ///< The game's own OpenGL driver.
    SCD3D11 = 3,         ///< SCD3D11.dll (Direct3D 11).
    Scvk = 4,            ///< scvk.dll (Vulkan).
};

/// Description of the active graphics driver.
struct SC4GraphicsDriverInfo
{
    SC4GraphicsDriverKind kind;
    /// The driver's GZCOM class ID (cIGZGDriver::GetGZCLSID).
    uint32_t classId;
    /// Display name, for example "SCD3D11 (Direct3D 11)". Never null.
    const char* name;
    /// Full path of the module that implements the driver, UTF-8. Never null, empty if unknown.
    const char* modulePath;
};
