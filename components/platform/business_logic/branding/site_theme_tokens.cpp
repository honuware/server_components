#include "business_logic/branding/site_theme_tokens.h"

#include "business_logic/branding/site_value_validation.h"
#include "util/secrets/secrets_helper.h"

namespace Branding {

// The seven-step ramps. Registering every step (rather than just the base) is
// what lets a studio supply a real palette instead of one colour the app then
// has to tint — the hover states, the soft banner fills and the badge tones all
// come from the neighbouring steps.
//
// Step 400 is the base each ramp's roles point at; the description says so, so
// a studio changing one colour knows which one to change.
#define RAMP(name, human)                                                      \
    {"site_theme_palette_" name "_100", "--palette-" name "-100",              \
     ThemeTokenType::Color, ThemeTokenGroup::Palette,                          \
     human " — lightest tint. Used for soft banner fills."},                   \
    {"site_theme_palette_" name "_200", "--palette-" name "-200",              \
     ThemeTokenType::Color, ThemeTokenGroup::Palette,                          \
     human " — light. Used for badge fills and outlines."},                     \
    {"site_theme_palette_" name "_300", "--palette-" name "-300",              \
     ThemeTokenType::Color, ThemeTokenGroup::Palette,                          \
     human " — light-mid."},                                                    \
    {"site_theme_palette_" name "_400", "--palette-" name "-400",              \
     ThemeTokenType::Color, ThemeTokenGroup::Palette,                          \
     human " — THE base shade. Change this one to re-brand."},                  \
    {"site_theme_palette_" name "_500", "--palette-" name "-500",              \
     ThemeTokenType::Color, ThemeTokenGroup::Palette,                           \
     human " — dark. Used for hover states and strong text."},                   \
    {"site_theme_palette_" name "_600", "--palette-" name "-600",              \
     ThemeTokenType::Color, ThemeTokenGroup::Palette, human " — darker."},      \
    {"site_theme_palette_" name "_700", "--palette-" name "-700",              \
     ThemeTokenType::Color, ThemeTokenGroup::Palette, human " — darkest."}

const std::vector<ThemeToken>& SiteThemeTokens() {
    static const std::vector<ThemeToken> tokens = {
        // ---- Layer 1: the palette ramps ----
        RAMP("primary", "Your brand colour"),
        RAMP("secondary", "Your accent colour (also the warning tone)"),
        RAMP("tertiary", "The success tone"),
        RAMP("quaternary", "The danger tone"),
        RAMP("quinary", "The information tone"),
        RAMP("grey", "Neutral greys — text, borders and backgrounds"),
        {"site_theme_palette_surface_tint", "--palette-surface-tint",
         ThemeTokenType::Color, ThemeTokenGroup::Palette,
         "The subtle fill behind table headers and inactive chips."},
        {"site_theme_palette_surface_subtle", "--palette-surface-subtle",
         ThemeTokenType::Color, ThemeTokenGroup::Palette,
         "A near-white row fill, one step off the page background."},

        // ---- Brand ----
        {"site_theme_primary", "--theme-primary", ThemeTokenType::Color,
         ThemeTokenGroup::Brand,
         "Buttons, links and anything that should read as your brand."},
        {"site_theme_on_primary", "--theme-on-primary", ThemeTokenType::Color,
         ThemeTokenGroup::Brand,
         "Text and icons drawn ON your brand colour. Needs to be readable on it."},
        {"site_theme_primary_hover", "--theme-primary-hover",
         ThemeTokenType::Color, ThemeTokenGroup::Brand,
         "Your brand colour when the pointer is over a button."},
        {"site_theme_accent", "--theme-accent", ThemeTokenType::Color,
         ThemeTokenGroup::Brand,
         "A second brand colour for highlights beside the main one."},
        {"site_theme_on_accent", "--theme-on-accent", ThemeTokenType::Color,
         ThemeTokenGroup::Brand, "Text drawn on the accent colour."},

        // ---- Surfaces and text ----
        {"site_theme_ink", "--theme-ink", ThemeTokenType::Color,
         ThemeTokenGroup::Surface, "Headings."},
        {"site_theme_text", "--theme-text", ThemeTokenType::Color,
         ThemeTokenGroup::Surface, "Ordinary body copy."},
        {"site_theme_text_muted", "--theme-text-muted", ThemeTokenType::Color,
         ThemeTokenGroup::Surface,
         "Secondary text — dates, captions, the quieter half of a row."},
        {"site_theme_surface", "--theme-surface", ThemeTokenType::Color,
         ThemeTokenGroup::Surface, "The fill behind cards and panels."},
        {"site_theme_surface_tint", "--theme-surface-tint",
         ThemeTokenType::Color, ThemeTokenGroup::Surface,
         "A slightly tinted surface — table headers, inactive chips."},
        {"site_theme_background", "--theme-background", ThemeTokenType::Color,
         ThemeTokenGroup::Surface, "The page background behind everything."},
        {"site_theme_border", "--theme-border", ThemeTokenType::Color,
         ThemeTokenGroup::Surface, "The line around cards, tables and inputs."},

        // ---- The dark shell ----
        {"site_theme_inverse_surface", "--theme-inverse-surface",
         ThemeTokenType::Color, ThemeTokenGroup::Shell,
         "The header and footer band. Dark by default — the strongest brand "
         "gesture on the page."},
        {"site_theme_on_inverse_surface", "--theme-on-inverse-surface",
         ThemeTokenType::Color, ThemeTokenGroup::Shell,
         "Text and your logo on that band. Must contrast with it."},

        // ---- Status tones. The MEANINGS are fixed; a studio tunes the shade. ----
        {"site_theme_success", "--theme-success", ThemeTokenType::Color,
         ThemeTokenGroup::Status, "Fill behind a \"confirmed\" or \"paid\" badge."},
        {"site_theme_on_success", "--theme-on-success", ThemeTokenType::Color,
         ThemeTokenGroup::Status, "Text on the success fill."},
        {"site_theme_warn", "--theme-warn", ThemeTokenType::Color,
         ThemeTokenGroup::Status, "Fill behind a \"waitlisted\" or \"pending\" badge."},
        {"site_theme_on_warn", "--theme-on-warn", ThemeTokenType::Color,
         ThemeTokenGroup::Status, "Text on the warning fill."},
        {"site_theme_danger", "--theme-danger", ThemeTokenType::Color,
         ThemeTokenGroup::Status, "Fill behind a \"cancelled\" or \"no-show\" badge."},
        {"site_theme_on_danger", "--theme-on-danger", ThemeTokenType::Color,
         ThemeTokenGroup::Status, "Text on the danger fill."},
        {"site_theme_info", "--theme-info", ThemeTokenType::Color,
         ThemeTokenGroup::Status, "Fill behind a neutral informational badge."},
        {"site_theme_on_info", "--theme-on-info", ThemeTokenType::Color,
         ThemeTokenGroup::Status, "Text on the information fill."},

        // ---- Radius ----
        {"site_theme_radius_control", "--radius-control", ThemeTokenType::Length,
         ThemeTokenGroup::Radius,
         "Corner rounding on buttons, inputs and chips. e.g. 4px"},
        {"site_theme_radius_panel", "--radius-panel", ThemeTokenType::Length,
         ThemeTokenGroup::Radius, "Corner rounding on inline panels and banners."},
        {"site_theme_radius_card", "--radius-card", ThemeTokenType::Length,
         ThemeTokenGroup::Radius, "Corner rounding on cards."},
        {"site_theme_radius_pill", "--radius-pill", ThemeTokenType::Length,
         ThemeTokenGroup::Radius,
         "Used where something should be fully rounded. 9999px means \"a pill\"."},

        // ---- Type roles (D13: a family NAME; the server composes the stack) ----
        {"site_theme_font_body", "--font-body", ThemeTokenType::FontFamily,
         ThemeTokenGroup::FontRole, "The font ordinary reading text is set in."},
        {"site_theme_font_heading", "--font-heading", ThemeTokenType::FontFamily,
         ThemeTokenGroup::FontRole,
         "The font for headings, buttons and badges."},
        {"site_theme_font_display", "--font-display", ThemeTokenType::FontFamily,
         ThemeTokenGroup::FontRole,
         "The font for the biggest page titles and hero lines."},

        // ---- The type scale (Phase 6B item 13 — these existed in the
        // stylesheet but were never registered, so nothing could change them) ----
        {"site_theme_text_xs", "--text-xs", ThemeTokenType::Length,
         ThemeTokenGroup::TypeScale, "Smallest text — badges and captions."},
        {"site_theme_text_sm", "--text-sm", ThemeTokenType::Length,
         ThemeTokenGroup::TypeScale, "Small text — dense rows and secondary copy."},
        {"site_theme_text_base", "--text-base", ThemeTokenType::Length,
         ThemeTokenGroup::TypeScale, "Ordinary body text, buttons and the top menu."},
        {"site_theme_text_lg", "--text-lg", ThemeTokenType::Length,
         ThemeTokenGroup::TypeScale, "Large text — card titles and big buttons."},
        {"site_theme_text_xl", "--text-xl", ThemeTokenType::Length,
         ThemeTokenGroup::TypeScale, "Section headings."},
        {"site_theme_text_2xl", "--text-2xl", ThemeTokenType::Length,
         ThemeTokenGroup::TypeScale, "Page titles — the biggest text on a page."},

        {"site_theme_weight_regular", "--weight-regular", ThemeTokenType::Weight,
         ThemeTokenGroup::TypeScale, "The weight ordinary text is set in. 400 is normal."},
        {"site_theme_weight_medium", "--weight-medium", ThemeTokenType::Weight,
         ThemeTokenGroup::TypeScale, "A slightly heavier weight for emphasis."},
        {"site_theme_weight_semibold", "--weight-semibold", ThemeTokenType::Weight,
         ThemeTokenGroup::TypeScale, "Heavier again — used sparingly."},
        {"site_theme_weight_bold", "--weight-bold", ThemeTokenType::Weight,
         ThemeTokenGroup::TypeScale, "Headings, buttons and badges. 700 is bold."},

        // ---- Weight ROLES (Polish Phase 2) — which weight a surface uses,
        // decoupled from what the scale steps above mean. Before these, the
        // menu was welded to the semibold step: making the menu lighter meant
        // redefining semibold for every heading, button and badge at once.
        // They sit in the TypeScale group (not FontRole) deliberately: the
        // editor renders that group in "How text looks", where the specimen
        // previews them live.
        {"site_theme_weight_menu", "--weight-menu", ThemeTokenType::Weight,
         ThemeTokenGroup::TypeScale,
         "Menu items in the top bar. 600 is semi bold."},
        {"site_theme_weight_heading", "--weight-heading", ThemeTokenType::Weight,
         ThemeTokenGroup::TypeScale,
         "Headings and bold text throughout the site."},
        {"site_theme_weight_display", "--weight-display", ThemeTokenType::Weight,
         ThemeTokenGroup::TypeScale,
         "The biggest page titles and hero lines."},
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
        case ThemeTokenType::Weight: {
            // CSS numeric weights: 1–1000 in practice, and only digits — a
            // keyword like "bold" would be valid CSS but breaks arithmetic
            // consumers and the editor's slider.
            if (value.size() > 4) {
                return false;
            }
            int weight = 0;
            for (char c : value) {
                if (c < '0' || c > '9') {
                    return false;
                }
                weight = weight * 10 + (c - '0');
            }
            return weight >= 1 && weight <= 1000;
        }
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
