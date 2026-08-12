#pragma once

#include <string_view>
#include <vector>

#include "business_logic/branding/site_value_validation.h"
#include "util/types.h"  // KeyValueTable

class Transaction;

namespace Secrets {
class SecretsHelper;
}

namespace Branding {

// Tenant Theming and Branding Phase 1 — the CONTENT-SLOT REGISTRY.
//
// One built SPA bundle serves N studios, so every piece of brand copy the shell
// renders has to arrive at runtime. Each slot is a config_secrets key (declared
// brand-free in util/secrets/secret_keys.h) plus the shape its value must take.
// This registry is the single source of truth for three things:
//   1. which keys GET /api/site_info publishes in its `content` object,
//   2. how each value is validated / repaired (D10),
//   3. which fields Phase 6's Site Theme editor offers.
//
// It is framework surface per D11: an app supplies its own default VALUES via
// its secret-defaults registration, never its own key list.
//
// Deliberately NOT here: display_name, website_url and logo_url. They are
// already top-level fields of the site_info response (tenancy Phase 7.1), and
// the tenancy plan's 7.4 note asks this payload to carry branding rather than
// duplicate it.
struct ContentSlot {
    // The config_secrets key. Doubles as the JSON field name inside `content`,
    // so the SPA needs no lookup table.
    std::string_view key;
    SlotType type;
};

// The registry, in the order the plan's Content-Slot Catalog lists it.
const std::vector<ContentSlot>& SiteContentSlots();

// Reads every registered slot from the resolved tenant's secrets and returns
// key -> value for ALL of them. A key that is unset, blank, or fails validation
// comes back as an empty string rather than being omitted: the payload shape
// stays constant, and the SPA's non-empty-wins merge leaves its bundled default
// in place (D10 — a junk value must not blank a page, and a missing value must
// not change the response's shape).
KeyValueTable LoadSiteContent(
    Secrets::SecretsHelper& secrets, Transaction& transaction);

}  // namespace Branding
