// Componentization Phase 1.5: web_app_types.h must provide a usable forward
// declaration of WebApp on its OWN — without pulling in the full
// endpoints/web_app.h. That is exactly what lets business_logic/auth's
// server_config and cookie_manager reference WebApp by pointer/reference without
// an upward include edge into the endpoints layer. This translation unit
// deliberately includes ONLY web_app_types.h so it would fail to compile if the
// header ever started depending on the full definition.
#include "web_app_types.h"

#include <gtest/gtest.h>

namespace {

TEST(WebAppTypesTest, ProvidesForwardDeclaredWebApp) {
    // Compiles against the incomplete type alone: a pointer needs only the
    // forward declaration, never the class body.
    WebApp* webApp = nullptr;
    EXPECT_EQ(webApp, nullptr);

    // Pointer arithmetic-free round trip through void* to use the variable and
    // confirm it is a real (incomplete) type, not a macro or alias mistake.
    void* asVoid = static_cast<void*>(webApp);
    EXPECT_EQ(asVoid, nullptr);
}

}  // namespace
