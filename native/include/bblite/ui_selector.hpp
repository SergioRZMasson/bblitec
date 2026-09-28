#pragma once

#include <bblite/ui_selector_match.hpp>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/Factory.h>
#include <RmlUi/Core/StyleSheet.h>
#include <RmlUi/Core/StyleSheetContainer.h>

namespace bbl::pal {
inline bool ui_selector_uses_test(const std::vector<UiSelectorStep>& steps,
                                  UiSelectorTestKind kind) {
    for (const auto& step : steps)
        for (const auto& test : step.tests) {
            if (test.kind == kind)
                return true;
            for (const auto& alternative : test.alternatives)
                if (ui_selector_uses_test(alternative, kind))
                    return true;
        }
    return false;
}

inline std::uint32_t ui_selector_sequence_specificity(const std::vector<UiSelectorStep>& steps) {
    std::uint32_t result = 0;
    for (const auto& step : steps)
        for (const auto& test : step.tests) {
            if (test.kind == UiSelectorTestKind::Where)
                continue;
            if (test.kind == UiSelectorTestKind::Not || test.kind == UiSelectorTestKind::Is ||
                test.kind == UiSelectorTestKind::Has) {
                std::uint32_t alternative = 0;
                for (const auto& sequence : test.alternatives)
                    alternative = std::max(alternative, ui_selector_sequence_specificity(sequence));
                result += alternative;
            } else
                result += test.kind == UiSelectorTestKind::Id    ? 0x10000u
                          : test.kind == UiSelectorTestKind::Tag ? 1u
                                                                 : 0x100u;
        }
    return result;
}

/** RmlUi owns live matching. Cache parsed rules, never their match results. */
class UiStyleSelectors {
public:
    void clear() { selectors.clear(); }
    void set(const std::vector<Rml::String>& sources) {
        if (sources.size() == selectors.size() &&
            std::equal(
                sources.begin(), sources.end(), selectors.begin(),
                [](const auto& source, const auto& entry) { return source == entry.source; }))
            return;
        selectors.clear();
        for (const auto& source : sources)
            selectors.push_back({source, {}});
    }
    bool matches(Rml::Element* element, std::size_t index) const {
        if (!element || !element->GetContext() || element->GetTagName() == "#text" ||
            element->GetPseudoElement() != Rml::Element::PseudoElement::None)
            return false;
        auto& entry = selectors.at(index);
        auto& sheet = entry.sheet;
        if (!sheet) {
            // A single inert declaration makes RmlUi index the selector. This sheet
            // is queried only; it is never installed on an element or document.
            sheet = Rml::Factory::InstanceStyleSheetString(entry.source + "{display:block;}");
            if (!sheet)
                throw std::runtime_error("RmlUi could not parse a live UI selector: " +
                                         entry.source);
            sheet->UpdateCompiledStyleSheet(element->GetContext());
        }
        return bool(sheet->GetCompiledStyleSheet()->GetElementDefinition(element));
    }

private:
    struct Entry {
        Rml::String source;
        Rml::SharedPtr<Rml::StyleSheetContainer> sheet;
    };
    mutable std::vector<Entry> selectors;
};

} // namespace bbl::pal
