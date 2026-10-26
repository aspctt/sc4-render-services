#include "GraphicsDriverIdentification.h"

#include <Windows.h>

#include <filesystem>
#include <vector>

#include "cIGZGDriver.h"

namespace {
    constexpr uint32_t kSCGDriverDirectX = 0xBADB6906;
    constexpr uint32_t kSCGDriverOpenGL = 0xC4554841;

    std::wstring ModulePath(const HMODULE module) {
        std::vector<wchar_t> buffer(MAX_PATH);
        while (true) {
            const DWORD length = GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
            if (length == 0) {
                return {};
            }
            if (length < buffer.size() - 1) {
                return std::wstring(buffer.data(), length);
            }
            buffer.resize(buffer.size() * 2);
        }
    }

    std::string ToUtf8(const std::wstring& text) {
        if (text.empty()) {
            return {};
        }
        const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                                             nullptr, 0, nullptr, nullptr);
        std::string result(static_cast<size_t>(size), '\0');
        WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), result.data(), size,
                            nullptr, nullptr);
        return result;
    }
}

IdentifiedGraphicsDriver IdentifyGraphicsDriver(cIGZGDriver* driver) {
    IdentifiedGraphicsDriver result;
    if (!driver) {
        return result;
    }
    result.classId = driver->GetGZCLSID();

    // The vtable is the driver's code, so the module holding it is the driver. The class ID
    // alone cannot tell the built-in driver from a replacement that registers the same ID.
    const void* vtable = *reinterpret_cast<void* const*>(driver);
    HMODULE owner = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            static_cast<LPCWSTR>(vtable), &owner)) {
        return result;
    }

    const std::wstring path = ModulePath(owner);
    result.modulePath = ToUtf8(path);

    if (owner == GetModuleHandleW(nullptr)) {
        if (result.classId == kSCGDriverDirectX) {
            result.kind = SC4GraphicsDriverKind::BuiltInDirectX;
            result.name = "the game's built-in DirectX 7 driver";
        }
        else if (result.classId == kSCGDriverOpenGL) {
            result.kind = SC4GraphicsDriverKind::BuiltInOpenGL;
            result.name = "the game's built-in OpenGL driver";
        }
        else {
            result.name = "an unknown driver built into the game";
        }
        return result;
    }

    const std::wstring file = std::filesystem::path(path).filename().wstring();
    if (_wcsicmp(file.c_str(), L"SCD3D11.dll") == 0) {
        result.kind = SC4GraphicsDriverKind::SCD3D11;
        result.name = "SCD3D11 (Direct3D 11)";
    }
    else if (_wcsicmp(file.c_str(), L"scvk.dll") == 0) {
        result.kind = SC4GraphicsDriverKind::Scvk;
        result.name = "scvk (Vulkan)";
    }
    else {
        result.name = "an unknown driver plugin";
    }
    return result;
}
