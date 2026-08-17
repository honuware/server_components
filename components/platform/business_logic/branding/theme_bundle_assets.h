#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace Branding {

// Tenant Theming Phase 9 — the asset-name rule, in ONE place.
//
// Every binary in a bundle is referenced by a bare filename sitting beside
// `theme.json`. That rule is what keeps importing an untrusted bundle safe:
// there is no path to traverse, so there is no path traversal. A name that is
// not a plain filename is REFUSED rather than sanitized — sanitizing invites
// the "what does this normalize to" argument that path bugs live in.
//
// The same rule closes zip-slip for free: a zip entry named `../../etc/passwd`
// simply is not a valid asset name.

// Longest asset filename accepted. Comfortably fits `StudioSans-700-italic.woff2`
// while staying well inside every filesystem's limit.
inline constexpr std::size_t kMaxAssetNameBytes = 64;

// Caps for a whole bundle. A bundle is a theme, not a media library; these stop
// an upload being used as a way to write an unbounded amount of data.
inline constexpr std::size_t kMaxBundleAssets = 64;
inline constexpr std::size_t kMaxBundleAssetBytes = 8u * 1024u * 1024u;
inline constexpr std::size_t kMaxBundleTotalBytes = 64u * 1024u * 1024u;
inline constexpr std::size_t kMaxBundleJsonBytes = 1024u * 1024u;

// `^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$` — and no `..` anywhere.
//
// The leading character may not be `.`, `-` or `_`, which is what rules out
// `.`, `..`, dotfiles, and names that look like command-line flags. `/`, `\`
// and `:` are outside the character set, so directory separators and Windows
// drive letters cannot appear, and neither can a URL scheme.
bool IsValidBundleAssetName(std::string_view name);

// Whether a slot value points at a file in this bundle rather than out at the
// web. The two forms are told apart by SHAPE: anything containing `://` is a
// URL, anything else that is a valid asset name is a bundled file.
//
// This is why the name rule forbids `:` — without that, `https://evil/x.png`
// and `logo.png` would need a wrapper object to distinguish, and the file
// format would be uglier for no gain.
bool IsExternalUrlValue(std::string_view value);
bool IsBundleAssetReference(std::string_view value);

// Case-insensitive duplicate detection. macOS and Windows would collapse
// `Logo.png` and `logo.png` into one file on export, so a bundle carrying both
// is rejected at the source rather than losing one silently.
//
// Returns the first colliding name, or "" when the set is clean.
std::string FindCaseInsensitiveDuplicate(const std::vector<std::string>& names);

// The filename extension for a stored image type ("jpeg"/"png"/...) or font
// format ("woff2"/...). Returns "" for anything unrecognised, so a caller
// cannot accidentally build `logo.` out of a blank.
std::string ExtensionForImageType(std::string_view imageType);
std::string ExtensionForFontFormat(std::string_view fontFormat);

// The image type these bytes actually ARE — "png", "jpeg", "gif", "webp",
// "svg" — or "" when they are not an image at all.
//
// The same rule fonts already follow (D14), for the same reason: an imported
// asset is stored and then served back from OUR origin, so what it is must be
// decided by its content and never by the name someone put in a zip. An asset
// that is neither a font nor an image is refused rather than stored.
std::string ImageTypeFromMagicBytes(std::string_view bytes);

// Build a deterministic, valid asset name from a stem and an extension,
// truncating the stem if needed. Deterministic because a round-trip must be
// byte-identical: the same photo must produce the same filename every export.
//
// Returns "" when `extension` is empty — a caller with an unknown type should
// fail rather than emit an extensionless asset.
std::string MakeAssetName(std::string_view stem, std::string_view extension);

// Lower-cases and strips anything outside the asset character set, so a studio
// name can be used as a filename stem. Never returns a name that would fail
// IsValidBundleAssetName when combined with a real extension.
std::string SanitizeAssetStem(std::string_view text);

}  // namespace Branding
