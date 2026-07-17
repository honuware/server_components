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
#include "endpoints/update_item.h"

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
    anchor = reinterpret_cast<AnchorFunc>(&UpdateItem);
}

}  // namespace Endpoints
