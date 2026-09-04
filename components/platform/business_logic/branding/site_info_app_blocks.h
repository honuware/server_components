#pragma once

#include <functional>
#include <string>
#include <vector>

#include "sql_util/database_access/database_helper.h"
#include "sql_util/database_access/transaction.h"
#include "util/json_value.h"

namespace Branding {

// APP-CONTRIBUTED BLOCKS ON /api/site_info.
//
// `site_info` is the one payload every page already fetches at boot, which
// makes it the natural place to answer "what does this tenant have?" without
// buying another round trip. But it is FRAMEWORK-owned, and the questions
// worth answering there are mostly app concepts — "does this studio have
// service providers?" means nothing to honuware.
//
// Dropping an app field into the framework response would be exactly the edge
// the componentization work exists to prevent: honuware would carry a column
// it cannot explain, and the next app would inherit it. So the app registers a
// BLOCK instead, and its JSON lands under `app.<name>` where its ownership is
// legible from the payload alone.
//
// This mirrors RegisterThemeBundleSection deliberately — same registration
// shape, same idempotent-by-name rule, same test-clearing hook — so there is
// one pattern to learn for "the app contributes to a framework surface".
//
// A block runs inside site_info's transaction. Keep it CHEAP: this is on the
// boot path of every page load, so it should answer a question, not build a
// list. Anything a page needs the contents of belongs behind its own endpoint.
//
// Both the transaction AND the DatabaseHelper are passed, because a table
// helper needs the second to be constructed and `Transaction` does not expose
// one. Passing them beats capturing a helper at registration time: blocks are
// registered once at startup and must not carry per-request state.
using SiteInfoBlockBuilder =
    std::function<Json::Value(Transaction&, DatabaseHelper)>;

struct SiteInfoBlock {
    std::string name;
    SiteInfoBlockBuilder build;
};

// Registration is process-global and idempotent by name: registering the same
// name twice REPLACES the earlier entry rather than duplicating it, so a test
// can install a stub without leaking into the next test.
void RegisterSiteInfoBlock(std::string name, SiteInfoBlockBuilder build);

// Every registered block, in registration order.
const std::vector<SiteInfoBlock>& SiteInfoBlocks();

// Drops every registration. Tests only — production registers once at startup.
void ClearSiteInfoBlocksForTest();

}  // namespace Branding
