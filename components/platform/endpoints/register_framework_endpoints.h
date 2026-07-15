#pragma once

namespace Endpoints {

// Anchors every FRAMEWORK (honuware_platform) endpoint translation unit into the
// final link so its self-registering RoutingBase runs and the route exists at
// runtime. Without this, MSVC / the GNU linker dead-strip the unreferenced
// endpoint TUs out of the honuware_platform static library and every framework
// route 404s.
//
// This is the framework analogue of the app's Endpoints::RegisterAllEndpoints()
// (which anchors the APP endpoint table in web_app.cpp). A host that wants the
// framework CRUD/admin/health endpoints — the honuware component test runner, an
// example server — calls this once at startup before its WebApp routes requests.
//
// In the knottyyoga app this call is unnecessary: web_app.cpp already anchors
// these same framework endpoints, so the app keeps calling only
// RegisterAllEndpoints(). (This TU is only linked when something references
// RegisterFrameworkEndpoints, so it is inert in builds that do not call it.)
void RegisterFrameworkEndpoints();

}  // namespace Endpoints
