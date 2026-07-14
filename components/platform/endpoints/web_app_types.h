#pragma once

// Lightweight forward declaration of the WebApp application wrapper, whose full
// definition lives in endpoints/web_app.h.
//
// Componentization Phase 1.5: consumers that reference WebApp only by pointer or
// reference — notably business_logic/auth's server_config and cookie_manager
// (framework code) — include THIS header instead of the full web_app.h. That
// keeps the auth layer from taking an upward include edge into the endpoints
// layer and from transitively dragging in <crow.h> plus the middleware guard
// headers (cloudfront_origin_guard, csrf_guard, security_headers) that web_app.h
// pulls in for the Crow AppType. Translation units that actually call WebApp's
// members (server_config.cpp, cookie_manager.cpp, endpoints, tests) include
// endpoints/web_app.h for the complete definition.
//
// web_app.h includes this header, so the forward declaration and the definition
// never drift. In Phase 2 this file lands in the web-core portion of the
// platform target alongside web_app.h.
class WebApp;
