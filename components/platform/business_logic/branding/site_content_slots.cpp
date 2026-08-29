#include "business_logic/branding/site_content_slots.h"

#include "util/secrets/secret_keys.h"
#include "util/secrets/secrets_helper.h"

namespace Branding {

const std::vector<ContentSlot>& SiteContentSlots() {
    static const std::vector<ContentSlot> slots = {
        {Secrets::kSiteLogoAlt, SlotType::Line},
        {Secrets::kSiteBrowserTitle, SlotType::Line},
        {Secrets::kSiteFaviconUrl, SlotType::Url},
        {Secrets::kSiteHeroHeadline, SlotType::Line},
        {Secrets::kSiteHeroSubline, SlotType::Line},
        {Secrets::kSiteHeroImageUrl, SlotType::Url},
        {Secrets::kSiteTaglineLines, SlotType::Lines},
        {Secrets::kSiteTaglineImageUrl, SlotType::Url},
        {Secrets::kSiteAddressLines, SlotType::Lines},
        {Secrets::kSiteContactEmail, SlotType::Line},
        {Secrets::kSiteAboutMarkdown, SlotType::Markdown},
        {Secrets::kSiteStartIntro, SlotType::Line},
        {Secrets::kSiteMembershipBlurb, SlotType::Line},
        {Secrets::kSiteSocialLinks, SlotType::Lines},
    };
    return slots;
}

KeyValueTable LoadSiteContent(
    Secrets::SecretsHelper& secrets, Transaction& transaction) {
    KeyValueTable content;
    for (const ContentSlot& slot : SiteContentSlots()) {
        std::string stored = secrets.LookupSecret(transaction, slot.key);
        content[std::string(slot.key)] =
            NormalizeSlotValue(slot.type, stored);
    }
    return content;
}

}  // namespace Branding
