#pragma once

#include "liberomnia/models.hpp"
#include "liberomnia/source_catalog.hpp"

#include <functional>
#include <optional>
#include <set>
#include <stop_token>
#include <string>
#include <vector>

namespace liberomnia::search {

enum class FilterSupport { native, local, unsupported };
struct ProviderCapabilities {
    std::set<domain::SearchField> fields;
    FilterSupport publication{FilterSupport::unsupported};
    FilterSupport language{FilterSupport::unsupported};
    FilterSupport availability{FilterSupport::unsupported};
};

enum class ProviderError { failed, rate_limited, blocked };
struct ProviderResult {
    std::vector<domain::BibliographicRecord> records;
    std::optional<ProviderError> error;
    // True only when the adapter has exhausted the matching upstream collection.
    bool exhaustive{false};
};

// search() starts asynchronous I/O and returns promptly. Stop callbacks must also
// return promptly; neither may wait for completion or execute blocking I/O.
// Complete once with a bounded result (including partial records on error).
// Acknowledging stop via completion releases the execution slot. The coordinator
// tolerates duplicate/late callbacks, but cannot forcibly stop a broken adapter.
class SearchProvider {
public:
    using Completion = std::function<void(ProviderResult)>;
    virtual ~SearchProvider() = default;
    [[nodiscard]] virtual const Source& source() const noexcept = 0;
    [[nodiscard]] virtual ProviderCapabilities capabilities() const = 0;
    virtual void search(domain::SearchRequest request, std::stop_token stop, Completion completion) = 0;
};

} // namespace liberomnia::search
