#pragma once

#include "liberomnia/models.hpp"
#include "liberomnia/source_catalog.hpp"

#include <functional>
#include <cstdint>
#include <optional>
#include <set>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace liberomnia::search {

enum class FilterSupport { native, local, unsupported };
struct ProviderCapabilities {
    std::set<domain::SearchField> fields;
    FilterSupport publication{FilterSupport::unsupported};
    FilterSupport language{FilterSupport::unsupported};
    FilterSupport availability{FilterSupport::unsupported};
};

enum class ProviderError { failed, rate_limited, blocked, network, authentication, invalid_response,
                           timed_out, cancelled, invalid_request, configuration_missing, configuration_invalid,
                           key_invalid, api_disabled, key_restricted, quota_daily_exhausted, quota_daily_zero,
                           rate_limit_minute };
// Public diagnostics are fixed messages, never upstream bodies or account metadata.
std::string_view provider_error_code(ProviderError error);
std::string_view provider_error_message(ProviderError error);
struct ProviderOperationalStatus {
    bool implemented{true};
    bool configured{};
    bool search_allowed{};
    std::string state;
    std::optional<std::int64_t> last_successful_at; // Unix seconds, observed locally.
    std::optional<ProviderError> error;
};
struct ProviderResult {
    std::vector<domain::BibliographicRecord> records;
    std::optional<ProviderError> error;
    // True only when the adapter has exhausted the matching upstream collection.
    bool exhaustive{false};
    bool fallback_used{false};
    bool result_limit_reached{false};
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
    [[nodiscard]] virtual ImplementationStatus implementation_status() const noexcept { return source().implementation_status; }
    [[nodiscard]] virtual std::optional<ProviderOperationalStatus> operational_status() const { return {}; }
    virtual void search(domain::SearchRequest request, std::stop_token stop, Completion completion) = 0;
};

} // namespace liberomnia::search
