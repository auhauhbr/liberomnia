#pragma once

#include "liberomnia/normalization.hpp"
#include "liberomnia/provider_registry.hpp"

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <variant>
#include <vector>

namespace liberomnia::search {

enum class SourceState { pending, running, completed, empty, rate_limited, blocked, failed,
                         timed_out, cancelled, unsupported, disabled, external_only };
enum class SessionState { pending, running, completed, partial, failed, cancelled };

struct SearchError {
    std::string code;
    std::string message;
    int http_status{500};
};
struct AppliedFilter {
    std::string id;
    FilterSupport support;
};
struct SourceProgress {
    std::string source_id;
    SourceState state{SourceState::pending};
    std::size_t records_retrieved{};
    std::size_t records_accepted{};
    std::size_t records_rejected{};
    bool search_completed{false};
    bool local_filters_applied{false};
    bool fallback_used{false};
    bool coverage_incomplete{true};
    bool result_limit_reached{false};
    std::vector<AppliedFilter> filters;
    std::optional<SearchError> error;
};
struct SessionSnapshot {
    std::string id;
    SessionState state{SessionState::pending};
    std::chrono::system_clock::time_point created_at;
    bool finished{false};
    bool cancellation_requested{false};
    bool has_errors{false};
    std::vector<SourceProgress> sources;
    std::vector<NormalizedRecord> records;
};
struct CoordinatorOptions {
    std::size_t max_concurrent_providers{4};
    std::size_t max_pending_work{64};
    std::size_t max_sessions{32};
    std::size_t max_records_per_session{200};
    std::chrono::milliseconds provider_timeout{8000};
    std::chrono::milliseconds session_ttl{900000};
    [[nodiscard]] bool is_valid() const noexcept;
};

using SessionResult = std::variant<SessionSnapshot, SearchError>;

class SearchCoordinator {
public:
    using Clock = std::function<std::chrono::steady_clock::time_point()>;
    explicit SearchCoordinator(std::shared_ptr<const ProviderRegistry> registry,
                               CoordinatorOptions options = {}, Clock now = std::chrono::steady_clock::now);
    ~SearchCoordinator();
    SearchCoordinator(const SearchCoordinator&) = delete;
    SearchCoordinator& operator=(const SearchCoordinator&) = delete;
    [[nodiscard]] SessionResult submit(domain::SearchRequest request);
    [[nodiscard]] SessionResult snapshot(const std::string& id, bool include_records = true);
    [[nodiscard]] SessionResult cancel(const std::string& id);
    // Opportunistic cleanup also allows deterministic tests with an injected clock.
    void sweep();
    [[nodiscard]] const ProviderRegistry& registry() const noexcept;
private:
    struct State;
    std::shared_ptr<State> state_;
    std::jthread worker_;
};

[[nodiscard]] std::string_view name(SourceState value);
[[nodiscard]] std::string_view name(SessionState value);
[[nodiscard]] std::string_view name(FilterSupport value);

} // namespace liberomnia::search
