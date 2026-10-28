#pragma once

#include "liberomnia/models.hpp"
#include "liberomnia/source_catalog.hpp"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace liberomnia::search {

struct ProviderResult {
    std::vector<domain::BibliographicRecord> records;
    std::optional<std::string> error;
};

// Future adapters complete exactly once, including on failure. No adapter exists yet.
// A future engine owns provider lifetimes and aggregates results off the HTTP loop.
class SearchProvider {
public:
    using Completion = std::function<void(ProviderResult)>;
    virtual ~SearchProvider() = default;
    [[nodiscard]] virtual const Source& source() const noexcept = 0;
    virtual void search(domain::SearchQuery query, Completion completion) = 0;
};

} // namespace liberomnia::search
