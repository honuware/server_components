#pragma once

#include <crow.h>
#include "util/json_value.h"

class EndpointAuthHelper;

namespace Endpoints {

// The font MANAGER (Tenant Theming Phase 6B / D17). Admin only.
//
// Site Theme edits `config_secrets`; this edits TABLES — `site_font_sources`,
// `site_fonts` and `site_font_faces`. Splitting the two surfaces on the storage
// boundary is what keeps each endpoint's validation honest: secrets go through
// the slot/token validators, rows go through the font validators, and binaries
// go through magic-byte checks.
//
//   GET    /api/manage/site_fonts                the whole inventory
//   PUT    /api/manage/site_fonts                replace sources + families
//   POST   /api/manage/site_font_face            upload one face (base64 body)
//   DELETE /api/manage/site_font_face/<id>       remove one face
//
// The PUT is a whole-inventory replace rather than per-row CRUD: the editor
// shows sources and families as one list, a family's meaning depends on which
// source it points at, and a partial save could leave a `cdn` family pointing
// at a source that is no longer there. Faces are separate because they carry
// binaries and are added one at a time.
Json::Value GetManageSiteFonts(
    EndpointAuthHelper& endpointAuthHelper,
    const crow::request& req,
    crow::response& resp);

Json::Value PutManageSiteFonts(
    EndpointAuthHelper& endpointAuthHelper,
    const crow::request& req,
    crow::response& resp);

Json::Value PostManageSiteFontFace(
    EndpointAuthHelper& endpointAuthHelper,
    const crow::request& req,
    crow::response& resp);

Json::Value DeleteManageSiteFontFace(
    EndpointAuthHelper& endpointAuthHelper,
    const crow::request& req,
    crow::response& resp,
    int64_t faceId);

}  // namespace Endpoints
