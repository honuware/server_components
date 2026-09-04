#include "business_logic/branding/site_info_app_blocks.h"

#include <mutex>

namespace Branding {
namespace {

// Function-local rather than a namespace-scope global: registration happens
// from static initializers in the app, and a function-local static is
// constructed on first use, which removes the initialization-order question
// entirely. (Same reasoning as theme_bundle_sections.cpp.)
std::vector<SiteInfoBlock>& MutableBlocks() {
    static std::vector<SiteInfoBlock> blocks;
    return blocks;
}

std::mutex& BlocksMutex() {
    static std::mutex mutex;
    return mutex;
}

}  // namespace

void RegisterSiteInfoBlock(std::string name, SiteInfoBlockBuilder build) {
    std::lock_guard<std::mutex> lock(BlocksMutex());
    std::vector<SiteInfoBlock>& blocks = MutableBlocks();
    for (SiteInfoBlock& block : blocks) {
        if (block.name == name) {
            // Replace rather than append: two registrations of one name would
            // otherwise both run and the second would overwrite the first's
            // JSON anyway, and a test installing a stub would leak into the
            // next test.
            block.build = std::move(build);
            return;
        }
    }
    blocks.push_back(SiteInfoBlock{std::move(name), std::move(build)});
}

const std::vector<SiteInfoBlock>& SiteInfoBlocks() {
    std::lock_guard<std::mutex> lock(BlocksMutex());
    return MutableBlocks();
}

void ClearSiteInfoBlocksForTest() {
    std::lock_guard<std::mutex> lock(BlocksMutex());
    MutableBlocks().clear();
}

}  // namespace Branding
