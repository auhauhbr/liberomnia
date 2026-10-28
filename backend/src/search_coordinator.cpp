#include "liberomnia/search_coordinator.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <stdexcept>
#include <sys/random.h>

namespace liberomnia::search {
namespace {
bool terminal(SourceState state) { return state != SourceState::pending && state != SourceState::running; }
bool successful(SourceState state) { return state == SourceState::completed || state == SourceState::empty; }
SearchError error(std::string code, std::string message, int status) {
    return {std::move(code), std::move(message), status};
}
std::string new_id() {
    std::array<unsigned char, 16> bytes{};
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const auto count = getrandom(bytes.data() + offset, bytes.size() - offset, GRND_NONBLOCK);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) throw std::runtime_error("Session ID generation failed");
        offset += static_cast<std::size_t>(count);
    }
    constexpr char hex[] = "0123456789abcdef";
    std::string result;
    for (auto byte : bytes) { result += hex[byte >> 4]; result += hex[byte & 15]; }
    return result;
}
std::string lowercase(std::string value) {
    for (char& c : value) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
    return value;
}
bool matches_local(const domain::BibliographicRecord& record, const domain::SearchRequest& request,
                   const ProviderCapabilities& caps) {
    if (caps.publication == FilterSupport::local && (request.publication.from || request.publication.to)) {
        if (!record.publication_year || (request.publication.from && *record.publication_year < *request.publication.from) ||
            (request.publication.to && *record.publication_year > *request.publication.to)) return false;
    }
    if (caps.language == FilterSupport::local && request.language &&
        (!record.language || lowercase(*record.language) != lowercase(*request.language))) return false;
    if (caps.availability == FilterSupport::local && request.availability) {
        if (record.access.empty()) return *request.availability == domain::AccessAvailability::unknown;
        return std::any_of(record.access.begin(), record.access.end(), [&](const auto& access) {
            return access.availability == *request.availability;
        });
    }
    return true;
}
} // namespace

bool CoordinatorOptions::is_valid() const noexcept {
    return max_concurrent_providers >= 1 && max_concurrent_providers <= 32 &&
        max_pending_work >= 1 && max_pending_work <= 1024 && max_sessions >= 1 && max_sessions <= 256 &&
        max_records_per_session >= 1 && max_records_per_session <= 1000 &&
        provider_timeout.count() >= 1 && provider_timeout <= std::chrono::minutes(5) &&
        session_ttl.count() >= 1 && session_ttl <= std::chrono::hours(24);
}

struct SearchCoordinator::State {
    struct Task {
        std::string session_id;
        std::string source_id;
        domain::SearchRequest request;
        domain::SearchRequest upstream_request;
        ProviderCapabilities capabilities;
        std::shared_ptr<SearchProvider> provider;
        std::size_t quota{};
        std::stop_source stop;
        std::chrono::steady_clock::time_point deadline;
        std::chrono::steady_clock::time_point received_at;
        bool cancel_requested{false};
        bool stop_dispatched{false};
        bool received{false};
        std::optional<ProviderResult> result;
    };
    struct Session {
        SessionSnapshot snapshot;
        std::chrono::steady_clock::time_point expires;
        std::map<std::string, std::vector<NormalizedRecord>> records;
    };
    std::shared_ptr<const ProviderRegistry> registry;
    CoordinatorOptions options;
    Clock now;
    std::mutex mutex;
    std::condition_variable changed;
    std::map<std::string, Session> sessions;
    std::deque<std::pair<std::string, std::chrono::steady_clock::time_point>> expired;
    std::deque<std::shared_ptr<Task>> pending;
    std::vector<std::shared_ptr<Task>> active;

    SourceProgress* progress(const std::shared_ptr<Task>& task) {
        const auto session = sessions.find(task->session_id);
        if (session == sessions.end()) return nullptr;
        for (auto& source : session->second.snapshot.sources) if (source.source_id == task->source_id) return &source;
        return nullptr;
    }
    void update(Session& session) {
        auto& snap = session.snapshot;
        snap.finished = std::all_of(snap.sources.begin(), snap.sources.end(), [](const auto& s) { return terminal(s.state); });
        snap.has_errors = std::any_of(snap.sources.begin(), snap.sources.end(), [](const auto& s) { return s.error.has_value(); });
        if (!snap.finished) {
            snap.state = std::any_of(snap.sources.begin(), snap.sources.end(), [](const auto& s) {
                return s.state != SourceState::pending;
            }) ? SessionState::running : SessionState::pending;
        } else if (snap.cancellation_requested) {
            snap.state = SessionState::cancelled;
        } else if (std::all_of(snap.sources.begin(), snap.sources.end(), [](const auto& s) { return successful(s.state); })) {
            snap.state = SessionState::completed;
        } else {
            const bool useful = std::any_of(snap.sources.begin(), snap.sources.end(), [](const auto& s) {
                return successful(s.state) || s.records_accepted > 0;
            });
            snap.state = useful ? SessionState::partial : SessionState::failed;
        }
    }
    void cancel_tasks(const std::string& id) {
        std::erase_if(pending, [&](const auto& task) { return task->session_id == id; });
        for (auto& task : active) if (task->session_id == id) task->cancel_requested = true;
    }
    void cleanup() {
        const auto current = now();
        while (!expired.empty() && expired.front().second <= current) expired.pop_front();
        for (auto it = sessions.begin(); it != sessions.end();) {
            if (it->second.expires <= current) {
                cancel_tasks(it->first);
                expired.emplace_back(it->first, current + options.session_ttl);
                it = sessions.erase(it);
            } else ++it;
        }
        while (expired.size() > options.max_sessions) expired.pop_front();
        for (auto& task : active) {
            auto* source = progress(task);
            if (source && source->state == SourceState::running && task->deadline <= current &&
                (!task->received || task->received_at >= task->deadline)) {
                source->state = SourceState::timed_out;
                source->error = error("provider_timed_out", "Provider deadline exceeded.", 504);
                task->cancel_requested = true;
                update(sessions.at(task->session_id));
            }
        }
    }
    SearchError missing(const std::string& id) const {
        const bool was_expired = std::any_of(expired.begin(), expired.end(), [&](const auto& item) { return item.first == id; });
        return was_expired ? error("session_expired", "Search session has expired.", 410)
                           : error("session_not_found", "Search session does not exist.", 404);
    }
    SessionSnapshot copy(const Session& session, bool records) const {
        auto result = session.snapshot;
        if (records) for (const auto& [id, entries] : session.records) {
            (void)id;
            result.records.insert(result.records.end(), entries.begin(), entries.end());
        }
        return result;
    }
    void accept(const std::shared_ptr<Task>& task) {
        auto* source = progress(task);
        if (!source || source->state != SourceState::running) return;
        auto& result = *task->result;
        source->records_retrieved = result.records.size();
        source->fallback_used = result.fallback_used;
        source->local_filters_applied = source->local_filters_applied || result.fallback_used;
        source->coverage_incomplete = !result.exhaustive || result.error.has_value() || source->local_filters_applied;
        std::map<std::string, NormalizedRecord> unique;
        for (auto& record : result.records) {
            auto normalized = normalize_record(std::move(record), task->source_id);
            if (!normalized) { ++source->records_rejected; source->coverage_incomplete = true; continue; }
            if (!matches_local(normalized->record, task->request, task->capabilities)) continue;
            // For duplicate IDs the first provider occurrence wins. Never merge editions across sources.
            const auto id = normalized->record.source_record_id;
            unique.try_emplace(id, std::move(*normalized));
        }
        auto& records = sessions.at(task->session_id).records[task->source_id];
        source->result_limit_reached = result.result_limit_reached || unique.size() > task->quota;
        for (auto& [id, record] : unique) {
            (void)id;
            if (records.size() == task->quota) break;
            records.push_back(std::move(record));
        }
        source->records_accepted = records.size();
        source->coverage_incomplete = source->coverage_incomplete || source->result_limit_reached;
        if (result.error) {
            switch (*result.error) {
                case ProviderError::rate_limited:
                    source->state = SourceState::rate_limited;
                    source->error = error("provider_rate_limited", "Provider rate limit reached.", 429); break;
                case ProviderError::blocked:
                    source->state = SourceState::blocked;
                    source->error = error("provider_blocked", "Provider denied access.", 403); break;
                case ProviderError::network:
                    source->state = SourceState::failed;
                    source->error = error("provider_network_error", "Unable to reach provider.", 502); break;
                case ProviderError::authentication:
                    source->state = SourceState::blocked;
                    source->error = error("provider_authentication", "Provider credentials were rejected.", 403); break;
                case ProviderError::invalid_response:
                    source->state = SourceState::failed;
                    source->error = error("invalid_provider_response", "Provider returned an invalid response.", 502); break;
                case ProviderError::invalid_request:
                    source->state = SourceState::failed;
                    source->error = error("provider_invalid_request", "Provider does not accept this language or query.", 422); break;
                case ProviderError::timed_out:
                    source->state = SourceState::timed_out;
                    source->error = error("provider_timed_out", "Provider deadline exceeded.", 504); break;
                case ProviderError::cancelled:
                    source->state = SourceState::cancelled;
                    source->error = error("provider_cancelled", "Provider search cancelled.", 499); break;
                case ProviderError::failed:
                    source->state = SourceState::failed;
                    source->error = error("provider_failed", "Provider request failed.", 502); break;
            }
        } else if (source->records_rejected && records.empty()) {
            source->state = SourceState::failed;
            source->error = error("invalid_provider_response", "Provider returned invalid records.", 502);
        } else {
            source->state = records.empty() ? SourceState::empty : SourceState::completed;
            source->search_completed = true;
        }
        update(sessions.at(task->session_id));
    }
    static void run(const std::shared_ptr<State>& state, std::stop_token stop) {
        std::unique_lock lock(state->mutex);
        while (!stop.stop_requested()) {
            state->cleanup();
            for (auto it = state->active.begin(); it != state->active.end();) {
                if ((*it)->received) { state->accept(*it); it = state->active.erase(it); }
                else ++it;
            }
            std::vector<std::shared_ptr<Task>> stops;
            for (auto& task : state->active) if (task->cancel_requested && !task->stop_dispatched) {
                task->stop_dispatched = true;
                stops.push_back(task);
            }
            std::vector<std::shared_ptr<Task>> starts;
            while (!state->pending.empty() && state->active.size() < state->options.max_concurrent_providers) {
                auto task = state->pending.front();
                state->pending.pop_front();
                auto* source = state->progress(task);
                if (!source || terminal(source->state)) continue;
                source->state = SourceState::running;
                task->deadline = state->now() + state->options.provider_timeout;
                state->active.push_back(task);
                starts.push_back(task);
                state->update(state->sessions.at(task->session_id));
            }
            lock.unlock();
            for (const auto& task : stops) task->stop.request_stop();
            for (const auto& task : starts) {
                auto complete = [weak_state = std::weak_ptr<State>(state), weak_task = std::weak_ptr<Task>(task)](ProviderResult result) {
                    const auto shared = weak_state.lock();
                    const auto job = weak_task.lock();
                    if (!shared || !job) return;
                    std::lock_guard guard(shared->mutex);
                    if (job->received) return;
                    // Adapters must cap their upstream response; reject contract violations.
                    if (result.records.size() > job->upstream_request.max_results_per_source) {
                        result.records.clear();
                        result.error = ProviderError::failed;
                        result.exhaustive = false;
                    }
                    job->received = true;
                    job->received_at = shared->now();
                    job->result = std::move(result);
                    shared->changed.notify_one();
                };
                try { task->provider->search(task->upstream_request, task->stop.get_token(), complete); }
                catch (...) { complete(ProviderResult{{}, ProviderError::failed, false}); }
            }
            lock.lock();
            state->changed.wait_for(lock, std::chrono::milliseconds(10), [&] {
                return stop.stop_requested() ||
                    (!state->pending.empty() && state->active.size() < state->options.max_concurrent_providers) ||
                    std::any_of(state->active.begin(), state->active.end(), [](const auto& task) {
                        return task->received || (task->cancel_requested && !task->stop_dispatched);
                    });
            });
        }
        const auto active = state->active;
        state->pending.clear();
        state->active.clear();
        state->sessions.clear();
        lock.unlock();
        for (const auto& task : active) task->stop.request_stop();
    }
};

SearchCoordinator::SearchCoordinator(std::shared_ptr<const ProviderRegistry> registry, CoordinatorOptions options, Clock now)
    : state_(std::make_shared<State>()) {
    if (!registry || !options.is_valid() || !now) throw std::invalid_argument("Invalid search coordinator dependencies or limits");
    state_->registry = std::move(registry);
    state_->options = options;
    state_->now = std::move(now);
    worker_ = std::jthread([state = state_](std::stop_token stop) { State::run(state, stop); });
}
SearchCoordinator::~SearchCoordinator() {
    worker_.request_stop();
    state_->changed.notify_one();
    if (worker_.joinable()) worker_.join();
}
const ProviderRegistry& SearchCoordinator::registry() const noexcept { return *state_->registry; }

SessionResult SearchCoordinator::submit(domain::SearchRequest request) {
    if (!request.is_valid()) return error("invalid_request", "Search parameters are invalid or exceed allowed limits.", 422);
    request.text = normalize_spaces(request.text);
    std::lock_guard lock(state_->mutex);
    state_->cleanup();
    const auto& entries = state_->registry->entries();
    if (request.source_ids.empty()) for (const auto& [id, entry] : entries) {
        (void)entry;
        request.source_ids.push_back(id);
    }
    if (request.source_ids.size() > 16) return error("too_many_sources", "Select at most 16 sources.", 422);
    std::sort(request.source_ids.begin(), request.source_ids.end());
    State::Session session;
    std::vector<std::shared_ptr<State::Task>> tasks;
    for (const auto& id : request.source_ids) {
        const auto found = entries.find(id);
        if (found == entries.end()) return error("unknown_source", "A selected source is unknown.", 422);
        const auto& entry = found->second;
        SourceProgress source;
        source.source_id = id;
        if (entry.source.integration_type == IntegrationType::external) source.state = SourceState::external_only;
        else if (!entry.adapter || entry.source.implementation_status != ImplementationStatus::available ||
                 entry.adapter->implementation_status() != ImplementationStatus::available) source.state = SourceState::disabled;
        else {
            const auto& caps = entry.capabilities;
            bool supported = caps.fields.contains(request.field);
            auto upstream = request;
            upstream.source_ids = {id};
            const auto filter = [&](std::string name, FilterSupport support) {
                source.filters.push_back({std::move(name), support});
                if (support == FilterSupport::unsupported) supported = false;
                if (support == FilterSupport::local) source.local_filters_applied = true;
            };
            if (request.publication.from || request.publication.to) {
                filter("publication", caps.publication);
                if (caps.publication != FilterSupport::native) upstream.publication = {};
            }
            if (request.language) {
                filter("language", caps.language);
                if (caps.language != FilterSupport::native) upstream.language.reset();
            }
            if (request.availability) {
                filter("availability", caps.availability);
                if (caps.availability != FilterSupport::native) upstream.availability.reset();
            }
            if (!supported) {
                source.state = SourceState::unsupported;
                source.error = error("unsupported_search", "Source does not support this search mode or filter.", 422);
            } else {
                auto task = std::make_shared<State::Task>();
                task->source_id = id;
                task->request = request;
                task->upstream_request = std::move(upstream);
                task->capabilities = caps;
                task->provider = entry.adapter;
                tasks.push_back(std::move(task));
            }
        }
        session.snapshot.sources.push_back(std::move(source));
    }
    if (tasks.empty()) {
        const bool unsupported = std::any_of(session.snapshot.sources.begin(), session.snapshot.sources.end(), [](const auto& s) {
            return s.state == SourceState::unsupported;
        });
        return unsupported ? error("unsupported_search", "No selected provider supports this search mode and filters.", 422)
            : error("no_available_providers", "No selected search provider is implemented and available.", 503);
    }
    if (state_->sessions.size() >= state_->options.max_sessions)
        return error("session_limit_reached", "Search session capacity reached. Try again later.", 429);
    if (state_->pending.size() + tasks.size() > state_->options.max_pending_work)
        return error("work_limit_reached", "Search work capacity reached. Try again later.", 429);
    if (tasks.size() > state_->options.max_records_per_session)
        return error("result_budget_exceeded", "Session record budget is smaller than the selected provider count.", 422);
    try {
        do { session.snapshot.id = new_id(); }
        while (state_->sessions.contains(session.snapshot.id));
    } catch (...) { return error("internal_error", "Unable to create search session.", 500); }
    session.snapshot.created_at = std::chrono::system_clock::now();
    session.expires = state_->now() + state_->options.session_ttl;
    for (std::size_t i = 0; i < tasks.size(); ++i) {
        tasks[i]->session_id = session.snapshot.id;
        tasks[i]->quota = std::min(request.max_results_per_source, state_->options.max_records_per_session / tasks.size() +
            (i < state_->options.max_records_per_session % tasks.size() ? 1U : 0U));
        state_->pending.push_back(tasks[i]);
    }
    session.snapshot.has_errors = std::any_of(session.snapshot.sources.begin(), session.snapshot.sources.end(),
        [](const auto& source) { return source.error.has_value(); });
    auto initial = session.snapshot;
    state_->sessions.emplace(initial.id, std::move(session));
    state_->changed.notify_one();
    return initial;
}

SessionResult SearchCoordinator::snapshot(const std::string& id, bool include_records) {
    std::lock_guard lock(state_->mutex);
    state_->cleanup();
    const auto found = state_->sessions.find(id);
    state_->changed.notify_one();
    return found == state_->sessions.end() ? SessionResult(state_->missing(id))
        : SessionResult(state_->copy(found->second, include_records));
}
SessionResult SearchCoordinator::cancel(const std::string& id) {
    std::lock_guard lock(state_->mutex);
    state_->cleanup();
    const auto found = state_->sessions.find(id);
    if (found == state_->sessions.end()) return state_->missing(id);
    auto& session = found->second;
    if (!session.snapshot.finished) {
        session.snapshot.cancellation_requested = true;
        for (auto& source : session.snapshot.sources) if (!terminal(source.state)) source.state = SourceState::cancelled;
        state_->cancel_tasks(id);
        state_->update(session);
    }
    state_->changed.notify_one();
    return state_->copy(session, false);
}
void SearchCoordinator::sweep() {
    std::lock_guard lock(state_->mutex);
    state_->cleanup();
    state_->changed.notify_one();
}

std::string_view name(SourceState value) {
    switch (value) {
        case SourceState::pending: return "pending"; case SourceState::running: return "running";
        case SourceState::completed: return "completed"; case SourceState::empty: return "empty";
        case SourceState::rate_limited: return "rate_limited"; case SourceState::blocked: return "blocked";
        case SourceState::failed: return "failed"; case SourceState::timed_out: return "timed_out";
        case SourceState::cancelled: return "cancelled"; case SourceState::unsupported: return "unsupported";
        case SourceState::disabled: return "disabled"; case SourceState::external_only: return "external_only";
    }
    return "failed";
}
std::string_view name(SessionState value) {
    switch (value) {
        case SessionState::pending: return "pending"; case SessionState::running: return "running";
        case SessionState::completed: return "completed"; case SessionState::partial: return "partial";
        case SessionState::failed: return "failed"; case SessionState::cancelled: return "cancelled";
    }
    return "failed";
}
std::string_view name(FilterSupport value) {
    switch (value) {
        case FilterSupport::native: return "native"; case FilterSupport::local: return "local";
        case FilterSupport::unsupported: return "unsupported";
    }
    return "unsupported";
}
} // namespace liberomnia::search
