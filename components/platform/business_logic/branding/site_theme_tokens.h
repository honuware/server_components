#pragma once

#include <string_view>
#include <vector>

#include "util/types.h"  // KeyValueTable

class Transaction;

namespace Secrets {
class SecretsHelper;
}

namespace Branding {

// Tenant Theming Phase 4 — the DESIGN-TOKEN REGISTRY.
//
// The SPA ships a complete default token set as CSS (`assets/styles/_tokens.scss`
// under `:root`). This registry is the per-tenant OVERRIDE layer: each entry maps
// a config_secrets key to the CSS custom property the boot applier writes onto
// `document.documentElement`, where an inline property beats the `:root` rule.
//
// Two layers, mirroring the stylesheet (and Ryan's Figma file):
//   * PALETTE (`--palette-primary-400`) — the raw ramps. Overriding one of these
//     re-brands everything, because every role is defined as `var(--palette-…)`
//     and custom properties resolve at use time. This is the cheap re-brand.
//   * ROLES (`--theme-primary`) — for a studio that wants one job to differ from
//     what its palette implies.
//
// Unlike the content slots, an unset token is OMITTED from the payload rather
// than sent empty: writing an empty custom property makes it invalid at
// computed-value time, which would break the very cascade the defaults provide.
// "Absent" is how a tenant says "keep the bundled default".
//
// Framework surface per D11 — CommunityFinder themes through the same registry.
// There are no default VALUES on either side: the stylesheet holds the defaults,
// so a token with no config_secrets row is the normal, expected state.
enum class ThemeTokenType {
    Color,       // #RRGGBB
    Length,      // 8px / 0.5rem / 0
    FontFamily,  // Roboto, Arial, sans-serif
};

struct ThemeToken {
    // config_secrets key, e.g. "site_theme_primary".
    std::string_view key;
    // The CSS custom property it overrides, e.g. "--theme-primary". The payload
    // is keyed by THIS, so the client applies it without a lookup table.
    std::string_view cssVariable;
    ThemeTokenType type;
};

// The registry. Palette entries first, then roles, then radius, then type.
const std::vector<ThemeToken>& SiteThemeTokens();

// True when `value` is acceptable for `type`. Empty is never valid here —
// clearing a token means deleting the row, not storing "".
bool IsValidThemeTokenValue(ThemeTokenType type, std::string_view value);

// Reads every registered token from the resolved tenant's secrets and returns
// cssVariable -> value for the ones that are SET AND VALID. A junk value is
// dropped rather than served (D10), so a hand-edited row degrades to the
// bundled default instead of breaking the page it appears on.
KeyValueTable LoadSiteTheme(
    Secrets::SecretsHelper& secrets, Transaction& transaction);

}  // namespace Branding
