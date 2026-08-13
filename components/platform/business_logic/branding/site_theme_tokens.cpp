#include "business_logic/branding/site_theme_tokens.h"

#include "business_logic/branding/site_value_validation.h"
#include "util/secrets/secrets_helper.h"

namespace Branding {

// The seven-step ramps. Registering every step (rather than just the base) is
// what lets a studio supply a real palette instead of one colour the app then
// has to tint — the hover states, the soft banner fills and the badge tones all
// come from the neighbouring steps.
#define RAMP(name)                                                          \
    {"site_theme_palette_" name "_100", "--palette-" name "-100", ThemeTokenType::Color}, \
    {"site_theme_palette_" name "_200", "--palette-" name "-200", ThemeTokenType::Color}, \
    {"site_theme_palette_" name "_300", "--palette-" name "-300", ThemeTokenType::Color}, \
    {"site_theme_palette_" name "_400", "--palette-" name "-400", ThemeTokenType::Color}, \
    {"site_theme_palette_" name "_500", "--palette-" name "-500", ThemeTokenType::Color}, \
    {"site_theme_palette_" name "_600", "--palette-" name "-600", ThemeTokenType::Color}, \
    {"site_theme_palette_" name "_700", "--palette-" name "-700", ThemeTokenType::Color}

const std::vector<ThemeToken>& SiteThemeTokens() {
    static const std::vector<ThemeToken> tokens = {
        // ---- Layer 1: the palette ramps ----
        RAMP("primary"),     // brand
        RAMP("secondary"),   // accent + warn
        RAMP("tertiary"),    // success
        RAMP("quaternary"),  // danger
        RAMP("quinary"),     // info
        RAMP("grey"),        // neutrals
        {"site_theme_palette_surface_tint", "--palette-surface-tint",
         ThemeTokenType::Color},
        {"site_theme_palette_surface_subtle", "--palette-surface-subtle",
         ThemeTokenType::Color},

        // ---- Layer 2: roles, for a studio that wants one job to differ from
        // what its palette implies. ----
        {"site_theme_primary", "--theme-primary", ThemeTokenType::Color},
        {"site_theme_on_primary", "--theme-on-primary", ThemeTokenType::Color},
        {"site_theme_primary_hover", "--theme-primary-hover", ThemeTokenType::Color},
        {"site_theme_accent", "--theme-accent", ThemeTokenType::Color},
        {"site_theme_on_accent", "--theme-on-accent", ThemeTokenType::Color},
        {"site_theme_ink", "--theme-ink", ThemeTokenType::Color},
        {"site_theme_text", "--theme-text", ThemeTokenType::Color},
        {"site_theme_text_muted", "--theme-text-muted", ThemeTokenType::Color},
        {"site_theme_surface", "--theme-surface", ThemeTokenType::Color},
        {"site_theme_surface_tint", "--theme-surface-tint", ThemeTokenType::Color},
        {"site_theme_background", "--theme-background", ThemeTokenType::Color},
        {"site_theme_border", "--theme-border", ThemeTokenType::Color},
        // The dark shell. A studio with a light header/footer re-points these two
        // and the whole chrome inverts.
        {"site_theme_inverse_surface", "--theme-inverse-surface",
         ThemeTokenType::Color},
        {"site_theme_on_inverse_surface", "--theme-on-inverse-surface",
         ThemeTokenType::Color},
        // Status tones. The SEMANTICS are fixed (D8/"never themable"); a tenant
        // tunes the shade only.
        {"site_theme_success", "--theme-success", ThemeTokenType::Color},
        {"site_theme_on_success", "--theme-on-success", ThemeTokenType::Color},
        {"site_theme_warn", "--theme-warn", ThemeTokenType::Color},
        {"site_theme_on_warn", "--theme-on-warn", ThemeTokenType::Color},
        {"site_theme_danger", "--theme-danger", ThemeTokenType::Color},
        {"site_theme_on_danger", "--theme-on-danger", ThemeTokenType::Color},
        {"site_theme_info", "--theme-info", ThemeTokenType::Color},
        {"site_theme_on_info", "--theme-on-info", ThemeTokenType::Color},

        // ---- Radius ----
        {"site_theme_radius_control", "--radius-control", ThemeTokenType::Length},
        {"site_theme_radius_panel", "--radius-panel", ThemeTokenType::Length},
        {"site_theme_radius_card", "--radius-card", ThemeTokenType::Length},
        {"site_theme_radius_pill", "--radius-pill", ThemeTokenType::Length},

        // ---- Type roles. The FAMILIES they name come from the tenant's
        // site_fonts inventory (D4); these three decide which family does which
        // job. ----
        {"site_theme_font_body", "--font-body", ThemeTokenType::FontFamily},
        {"site_theme_font_heading", "--font-heading", ThemeTokenType::FontFamily},
        {"site_theme_font_display", "--font-display", ThemeTokenType::FontFamily},
    };
    return tokens;
}

#undef RAMP

bool IsValidThemeTokenValue(ThemeTokenType type, std::string_view value) {
    if (value.empty()) {
        return false;
    }
    switch (type) {
        case ThemeTokenType::Color:
            return IsValidHexColor(value);
        case ThemeTokenType::Length:
            return IsValidCssLength(value);
        case ThemeTokenType::FontFamily:
            return IsValidFontFamilyList(value);
    }
    return false;
}

KeyValueTable LoadSiteTheme(
    Secrets::SecretsHelper& secrets, Transaction& transaction) {
    KeyValueTable theme;
    for (const ThemeToken& token : SiteThemeTokens()) {
        std::string stored = secrets.LookupSecret(transaction, token.key);
        if (!IsValidThemeTokenValue(token.type, stored)) {
            // Unset or junk — leave it out so the stylesheet's default stands.
            continue;
        }
        theme[std::string(token.cssVariable)] = stored;
    }
    return theme;
}

}  // namespace Branding
