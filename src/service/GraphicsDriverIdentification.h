#pragma once

#include <string>

#include "public/GraphicsDriverInfo.h"

class cIGZGDriver;

struct IdentifiedGraphicsDriver
{
    SC4GraphicsDriverKind kind = SC4GraphicsDriverKind::Unknown;
    uint32_t classId = 0;
    const char* name = "unknown";
    std::string modulePath;  // UTF-8
};

// Identifies a driver by the module its vtable lives in. Cheap enough per driver object, but
// callers should cache the result rather than run it every frame.
IdentifiedGraphicsDriver IdentifyGraphicsDriver(cIGZGDriver* driver);
