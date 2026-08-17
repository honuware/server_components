#include "endpoints/register_framework_endpoints.h"

// Take the address of every framework endpoint function below so MSVC / the GNU
// linker cannot dead-strip its translation unit (and the self-registering
// RoutingBase it carries) from the honuware_platform static library. This mirrors
// the app's web_app.cpp registration table, but for the framework generic-CRUD,
// admin-metadata, and health endpoints only.
#include "endpoints/add_item.h"
#include "endpoints/add_item_fetch_primary_key.h"
#include "endpoints/db_schema.h"
#include "endpoints/delete_item.h"
#include "endpoints/get_filtered_table_rows.h"
#include "endpoints/get_fk_options.h"
#include "endpoints/get_row.h"
#include "endpoints/get_row_by_values.h"
#include "endpoints/get_rows_by_column.h"
#include "endpoints/get_table_rows.h"
#include "endpoints/health.h"
#include "endpoints/resolve_fk_display.h"
#include "endpoints/site_info.h"
#include "endpoints/site_font_face.h"
#include "endpoints/site_asset.h"
#include "endpoints/manage_site_theme.h"
#include "endpoints/manage_site_fonts.h"
#include "endpoints/manage_site_theme_bundle.h"
#include "endpoints/update_item.h"
// Phase 0.2a (H8): account/user + photo endpoints extracted from the app.
#include "endpoints/account_activation.h"
#include "endpoints/verify.h"
#include "endpoints/login.h"
#include "endpoints/logout.h"
#include "endpoints/me.h"
#include "endpoints/remember.h"
#include "endpoints/get_user_info.h"
#include "endpoints/set_user_info.h"
#include "endpoints/update_user_password.h"
#include "endpoints/has_photo.h"
#include "endpoints/upload_photo.h"
#include "endpoints/upload_user_photo.h"
#include "endpoints/delete_photo.h"
#include "endpoints/get_photo.h"
// Phase 0.2b (H8): register + get_scaled_photo (with framework seams).
#include "endpoints/register.h"
#include "endpoints/get_scaled_photo.h"

namespace Endpoints {

// Anchor every framework endpoint's translation unit into the final link.
//
// Each endpoint .cpp registers its route through a file-scope, self-registering
// RoutingBase object. Those objects live in the honuware_platform STATIC library,
// and a static archive only yields an object file when the linker needs it to
// resolve an undefined symbol. Without a genuine reference into each TU, the
// object is never extracted, its route is never registered, and every request to
// that endpoint silently 404s.
//
// THE ADDRESSES MUST BE TAKEN INSIDE THIS FUNCTION, AND THE VOLATILE STORE IS
// LOAD-BEARING. This previously took each address into an unused
// anonymous-namespace variable at file scope:
//
//     namespace { auto g_AddItem = &Endpoints::AddItem; }   // NOT enough
//
// which works at -O0 but NOT at -O2: those variables are unused and have internal
// linkage, so the optimiser deletes them outright, the relocation disappears with
// them, the linker never extracts the object, and every framework route 404s in a
// RELEASE build. Debug keeps the variables, which is why Windows/Debug never saw
// it. Verified with the old form on gcc 14 -O2: `nm` showed
// RegisterFrameworkEndpoints() present but Endpoints::AddItem entirely ABSENT
// from the binary, while add_item.cpp.o sat in libhonuware_platform.a defining it
// — and 63 endpoint tests failed with 404.
//
// A store through a volatile object is an observable side effect the compiler may
// not elide, so each address-of below survives optimisation as a real relocation.
// Casting between function pointer types is well-defined; these are only ever
// stored, never called through.
void RegisterFrameworkEndpoints() {
    using AnchorFunc = void (*)();
    static AnchorFunc volatile anchor = nullptr;

    anchor = reinterpret_cast<AnchorFunc>(&AddItem);
    anchor = reinterpret_cast<AnchorFunc>(&AddItemFetchPrimaryKey);
    anchor = reinterpret_cast<AnchorFunc>(&GetDbSchema);
    anchor = reinterpret_cast<AnchorFunc>(&DeleteItem);
    anchor = reinterpret_cast<AnchorFunc>(&GetFilteredTableRows);
    anchor = reinterpret_cast<AnchorFunc>(&GetFkOptions);
    anchor = reinterpret_cast<AnchorFunc>(&ResolveFkDisplay);
    anchor = reinterpret_cast<AnchorFunc>(&GetRowsByColumn);
    anchor = reinterpret_cast<AnchorFunc>(&GetRow);
    anchor = reinterpret_cast<AnchorFunc>(&GetRowByValues);
    anchor = reinterpret_cast<AnchorFunc>(&GetTableRows);
    anchor = reinterpret_cast<AnchorFunc>(&GetHealth);
    anchor = reinterpret_cast<AnchorFunc>(&GetSiteInfo);
    anchor = reinterpret_cast<AnchorFunc>(&GetSiteFontFace);
    anchor = reinterpret_cast<AnchorFunc>(&GetManageSiteTheme);
    anchor = reinterpret_cast<AnchorFunc>(&PutManageSiteTheme);
    anchor = reinterpret_cast<AnchorFunc>(&GetManageSiteFonts);
    anchor = reinterpret_cast<AnchorFunc>(&PutManageSiteFonts);
    anchor = reinterpret_cast<AnchorFunc>(&PostManageSiteFontFace);
    anchor = reinterpret_cast<AnchorFunc>(&DeleteManageSiteFontFace);
    // Phase 9 — theme bundles, plus the route their images are served from.
    anchor = reinterpret_cast<AnchorFunc>(&GetSiteAsset);
    anchor = reinterpret_cast<AnchorFunc>(&GetManageSiteThemeBundle);
    anchor = reinterpret_cast<AnchorFunc>(&PostManageSiteThemeBundleValidate);
    anchor = reinterpret_cast<AnchorFunc>(&PostManageSiteThemeBundle);
    anchor = reinterpret_cast<AnchorFunc>(&UpdateItem);

    // Phase 0.2a (H8): account/user + photo endpoints extracted from the app.
    anchor = reinterpret_cast<AnchorFunc>(&AccountActivation);
    anchor = reinterpret_cast<AnchorFunc>(&Verify);
    anchor = reinterpret_cast<AnchorFunc>(&Login);
    anchor = reinterpret_cast<AnchorFunc>(&Logout);
    anchor = reinterpret_cast<AnchorFunc>(&Me);
    anchor = reinterpret_cast<AnchorFunc>(&Remember);
    anchor = reinterpret_cast<AnchorFunc>(&GetUserInfo);
    anchor = reinterpret_cast<AnchorFunc>(&SetUserInfo);
    anchor = reinterpret_cast<AnchorFunc>(&UpdateUserPassword);
    anchor = reinterpret_cast<AnchorFunc>(&GetHasPhoto);
    anchor = reinterpret_cast<AnchorFunc>(&PostUploadPhoto);
    anchor = reinterpret_cast<AnchorFunc>(&PostUploadUserPhoto);
    anchor = reinterpret_cast<AnchorFunc>(&DeletePhoto);
    anchor = reinterpret_cast<AnchorFunc>(&GetPhoto);

    // Phase 0.2b (H8): register + get_scaled_photo.
    anchor = reinterpret_cast<AnchorFunc>(&Register);
    anchor = reinterpret_cast<AnchorFunc>(&GetScaledPhoto);
}

}  // namespace Endpoints
