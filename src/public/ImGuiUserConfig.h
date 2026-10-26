#pragma once

// Dear ImGui user config for SC4RenderServices, selected through IMGUI_USER_CONFIG so the
// upstream imconfig.h stays untouched. Every module that includes imgui.h must see the
// same config, so the build passes IMGUI_USER_CONFIG to all of them.

#if defined(IMGUI_DLL_EXPORT)
#define IMGUI_API __declspec(dllexport)
#elif defined(IMGUI_DLL_IMPORT)
#define IMGUI_API __declspec(dllimport)
#endif
