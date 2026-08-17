#include "business_logic/branding/theme_bundle_sections.h"

#include <mutex>

namespace Branding {
namespace {

// Function-local rather than a namespace-scope global: registration happens
// from static initializers in the app, and a function-local static is
// constructed on first use, which removes the initialization-order question
// entirely.
std::vector<ThemeBundleSection>& MutableSections() {
    static std::vector<ThemeBundleSection> sections;
    return sections;
}

std::mutex& SectionsMutex() {
    static std::mutex mutex;
    return mutex;
}

}  // namespace

void RegisterThemeBundleSection(
    std::string name, SectionExporter exporter, SectionImporter importer) {
    std::lock_guard<std::mutex> lock(SectionsMutex());
    std::vector<ThemeBundleSection>& sections = MutableSections();
    for (ThemeBundleSection& section : sections) {
        if (section.name == name) {
            // Replace rather than append. Two registrations of one name would
            // otherwise export the section twice and import it twice, and a
            // test installing a stub would leak into the next test.
            section.exporter = std::move(exporter);
            section.importer = std::move(importer);
            return;
        }
    }
    sections.push_back(
        ThemeBundleSection{std::move(name), std::move(exporter), std::move(importer)});
}

const std::vector<ThemeBundleSection>& ThemeBundleSections() {
    std::lock_guard<std::mutex> lock(SectionsMutex());
    return MutableSections();
}

const ThemeBundleSection* FindThemeBundleSection(const std::string& name) {
    std::lock_guard<std::mutex> lock(SectionsMutex());
    for (const ThemeBundleSection& section : MutableSections()) {
        if (section.name == name) {
            return &section;
        }
    }
    return nullptr;
}

void ClearThemeBundleSectionsForTest() {
    std::lock_guard<std::mutex> lock(SectionsMutex());
    MutableSections().clear();
}

}  // namespace Branding
