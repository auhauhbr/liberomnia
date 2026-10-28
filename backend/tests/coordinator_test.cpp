#include "liberomnia/search_coordinator.hpp"
#include "liberomnia/source_catalog.hpp"
#include "test_provider.hpp"

#include <gtest/gtest.h>
#include <atomic>
#include <set>

using namespace liberomnia;
using namespace liberomnia::search;
using liberomnia::test::ControlledProvider;
using liberomnia::test::record;
namespace {

auto registry(std::vector<std::shared_ptr<ControlledProvider>> providers, std::vector<Source> extra = {}) {
    std::vector<std::shared_ptr<SearchProvider>> adapters;
    for (const auto& provider : providers) { extra.push_back(provider->source()); adapters.push_back(provider); }
    return std::make_shared<ProviderRegistry>(std::move(extra), std::move(adapters));
}
domain::SearchRequest request() { domain::SearchRequest r; r.text = "books"; return r; }
std::string submit(SearchCoordinator& coordinator, domain::SearchRequest query = request()) {
    auto result = coordinator.submit(std::move(query));
    if (const auto* error = std::get_if<SearchError>(&result)) throw std::runtime_error(error->code);
    return std::get<SessionSnapshot>(result).id;
}
SessionSnapshot snapshot(SearchCoordinator& coordinator, const std::string& id) {
    return std::get<SessionSnapshot>(coordinator.snapshot(id));
}
// Dispatch and completion use explicit gates; this deadline only guards against a deadlock.
// Provider timeouts/expiry below use an injected clock, never elapsed wall-clock sleeps.
template <typename Predicate> void await(Predicate predicate) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= deadline) throw std::runtime_error("Condition did not become true");
        std::this_thread::yield();
    }
}
SessionSnapshot finished(SearchCoordinator& coordinator, const std::string& id) {
    await([&] { return snapshot(coordinator, id).finished; });
    return snapshot(coordinator, id);
}
struct ManualClock {
    std::atomic<std::int64_t> milliseconds{0};
    auto now() { return std::chrono::steady_clock::time_point(std::chrono::milliseconds(milliseconds.load())); }
};
}

TEST(Coordinator, NoRealProvidersAndUnknownSourcesDoNotCreateSessions) {
    SearchCoordinator coordinator(std::make_shared<ProviderRegistry>(source_catalog()));
    EXPECT_FALSE(coordinator.registry().search_available());
    EXPECT_EQ(std::get<SearchError>(coordinator.submit(request())).http_status, 503);
    auto query = request(); query.source_ids = {"nonexistent"};
    EXPECT_EQ(std::get<SearchError>(coordinator.submit(query)).code, "unknown_source");
    query.text = "  ";
    EXPECT_EQ(std::get<SearchError>(coordinator.submit(query)).code, "invalid_request");
    EXPECT_EQ(std::get<SearchError>(coordinator.snapshot("missing")).http_status, 404);
    EXPECT_EQ(std::get<SearchError>(coordinator.cancel("missing")).http_status, 404);
}

TEST(Coordinator, SelectionAndCapabilitiesAreEnforced) {
    auto a = std::make_shared<ControlledProvider>("a");
    auto b = std::make_shared<ControlledProvider>("b");
    SearchCoordinator coordinator(registry({a, b}));
    auto query = request(); query.field = domain::SearchField::passage;
    EXPECT_EQ(std::get<SearchError>(coordinator.submit(query)).code, "unsupported_search");
    query.field = domain::SearchField::title; query.language = "pt";
    EXPECT_EQ(std::get<SearchError>(coordinator.submit(query)).code, "unsupported_search");
    query.language.reset(); query.source_ids = {"b"};
    const auto id = submit(coordinator, query);
    b->wait_call().complete({{record("b")}, {}, true});
    EXPECT_EQ(finished(coordinator, id).sources.size(), 1U);
    EXPECT_EQ(a->call_count(), 0U);
}

TEST(Coordinator, NativeLocalAndUnsupportedFiltersRemainDistinct) {
    auto local = std::make_shared<ControlledProvider>("local");
    auto native = std::make_shared<ControlledProvider>("native");
    auto unsupported = std::make_shared<ControlledProvider>("unsupported");
    local->caps.publication = FilterSupport::local;
    local->caps.language = FilterSupport::local;
    native->caps.publication = FilterSupport::native;
    native->caps.language = FilterSupport::native;
    SearchCoordinator coordinator(registry({native, unsupported, local}));
    auto query = request(); query.publication = {1801, 1900}; query.language = "pt";
    const auto id = submit(coordinator, query);
    const auto l = local->wait_call(); const auto n = native->wait_call();
    EXPECT_FALSE(l.request.publication.from); EXPECT_FALSE(l.request.language);
    EXPECT_EQ(n.request.publication.from, 1801); EXPECT_EQ(n.request.language, "pt");
    auto low = record("local", "low", 1800); low.language = "pt";
    auto first = record("local", "first", 1801); first.language = "PT";
    auto last = record("local", "last", 1900); last.language = "pt";
    auto high = record("local", "high", 1901); high.language = "pt";
    auto unknown = record("local", "unknown"); unknown.publication_year.reset(); unknown.language = "pt";
    auto english = record("local", "english"); english.language = "en";
    l.complete({{low, first, last, high, unknown, english}, {}, true});
    n.complete({{}, {}, true});
    auto result = finished(coordinator, id);
    ASSERT_EQ(result.records.size(), 2U);
    EXPECT_EQ(result.sources[0].records_retrieved, 6U);
    EXPECT_TRUE(result.sources[0].local_filters_applied);
    EXPECT_TRUE(result.sources[0].coverage_incomplete);
    EXPECT_EQ(result.sources[0].filters[0].support, FilterSupport::local);
    EXPECT_FALSE(result.sources[1].coverage_incomplete);
    EXPECT_EQ(result.sources[2].state, SourceState::unsupported);
    EXPECT_EQ(result.state, SessionState::partial);
    EXPECT_EQ(unsupported->call_count(), 0U);
}

TEST(Coordinator, LocalAvailabilityDoesNotInventDownloads) {
    auto a = std::make_shared<ControlledProvider>("a"); a->caps.availability = FilterSupport::local;
    SearchCoordinator coordinator(registry({a}));
    auto query = request(); query.availability = domain::AccessAvailability::pdf;
    const auto id = submit(coordinator, query);
    auto pdf = record("a", "pdf"); pdf.access.push_back({domain::AccessAvailability::pdf, "https://example.org/book.pdf"});
    auto metadata = record("a", "metadata"); metadata.access.push_back({domain::AccessAvailability::metadata_only, {}});
    const auto call = a->wait_call(); EXPECT_FALSE(call.request.availability);
    call.complete({{record("a", "unknown"), pdf, metadata}, {}, false});
    const auto result = finished(coordinator, id);
    ASSERT_EQ(result.records.size(), 1U);
    EXPECT_EQ(result.records[0].record.source_record_id, "pdf");
    EXPECT_TRUE(result.sources[0].coverage_incomplete);
}

TEST(Coordinator, ConcurrentSlowAndFastProvidersPreserveProgress) {
    auto slow = std::make_shared<ControlledProvider>("slow");
    auto fast = std::make_shared<ControlledProvider>("fast");
    SearchCoordinator coordinator(registry({slow, fast}));
    const auto id = submit(coordinator);
    const auto s = slow->wait_call(); const auto f = fast->wait_call();
    f.complete({{record("fast")}, {}, true});
    await([&] { return snapshot(coordinator, id).records.size() == 1; });
    const auto progress = snapshot(coordinator, id);
    EXPECT_FALSE(progress.finished);
    EXPECT_EQ(progress.sources[1].state, SourceState::running);
    s.complete({{record("slow")}, {}, true});
    EXPECT_EQ(finished(coordinator, id).records.size(), 2U);
}

TEST(Coordinator, ConcurrencyAndPendingLimitsAreGlobal) {
    auto a = std::make_shared<ControlledProvider>("a");
    CoordinatorOptions options; options.max_concurrent_providers = 1; options.max_pending_work = 1;
    SearchCoordinator coordinator(registry({a}), options);
    const auto first = submit(coordinator);
    const auto call = a->wait_call();
    const auto second = submit(coordinator);
    EXPECT_EQ(a->call_count(), 1U);
    EXPECT_EQ(snapshot(coordinator, second).sources[0].state, SourceState::pending);
    EXPECT_EQ(std::get<SearchError>(coordinator.submit(request())).code, "work_limit_reached");
    call.complete({{}, {}, true});
    a->wait_call(1).complete({{}, {}, true});
    EXPECT_TRUE(finished(coordinator, first).finished);
    EXPECT_TRUE(finished(coordinator, second).finished);
}

TEST(Coordinator, PartialFailuresNeverEraseOtherResults) {
    auto a = std::make_shared<ControlledProvider>("a");
    auto b = std::make_shared<ControlledProvider>("b");
    auto c = std::make_shared<ControlledProvider>("c");
    SearchCoordinator coordinator(registry({a, b, c}));
    const auto id = submit(coordinator);
    a->wait_call().complete({{record("a")}, {}, true});
    b->wait_call().complete({{record("b")}, ProviderError::rate_limited, false});
    c->wait_call().complete({{}, ProviderError::blocked, false});
    const auto result = finished(coordinator, id);
    EXPECT_EQ(result.records.size(), 2U);
    EXPECT_EQ(result.state, SessionState::partial);
    EXPECT_TRUE(result.has_errors);
    EXPECT_EQ(result.sources[1].state, SourceState::rate_limited);
    EXPECT_EQ(result.sources[2].state, SourceState::blocked);
    EXPECT_FALSE(result.sources[1].search_completed);
}

TEST(Coordinator, TimeoutIsNotEmptyAndDoesNotReleaseUnacknowledgedWork) {
    auto a = std::make_shared<ControlledProvider>("a");
    CoordinatorOptions options; options.provider_timeout = std::chrono::milliseconds(10); options.max_concurrent_providers = 1;
    ManualClock clock;
    SearchCoordinator coordinator(registry({a}), options, [&] { return clock.now(); });
    const auto id = submit(coordinator);
    const auto call = a->wait_call();
    clock.milliseconds = 11; coordinator.sweep();
    const auto result = finished(coordinator, id);
    EXPECT_EQ(result.sources[0].state, SourceState::timed_out);
    EXPECT_EQ(result.state, SessionState::failed);
    await([&] { return call.stop.stop_requested(); });
    const auto next = submit(coordinator);
    EXPECT_EQ(a->call_count(), 1U);
    call.complete({{record("a")}, {}, true});
    a->wait_call(1).complete({{}, {}, true});
    EXPECT_TRUE(finished(coordinator, next).finished);
    EXPECT_TRUE(snapshot(coordinator, id).records.empty());
    EXPECT_EQ(snapshot(coordinator, id).sources[0].state, SourceState::timed_out);
}

TEST(Coordinator, CancelPreservesFinishedResultsAndIgnoresLateOrDuplicateCallbacks) {
    auto a = std::make_shared<ControlledProvider>("a");
    auto b = std::make_shared<ControlledProvider>("b");
    SearchCoordinator coordinator(registry({a, b}));
    const auto id = submit(coordinator);
    const auto ca = a->wait_call(); const auto cb = b->wait_call();
    ca.complete({{record("a")}, {}, true});
    await([&] { return snapshot(coordinator, id).sources[0].state == SourceState::completed; });
    EXPECT_EQ(std::get<SessionSnapshot>(coordinator.cancel(id)).state, SessionState::cancelled);
    await([&] { return cb.stop.stop_requested(); });
    cb.complete({{record("b")}, {}, true});
    ca.complete({{record("a", "duplicate")}, {}, true});
    cb.complete({{}, ProviderError::failed, false});
    const auto result = snapshot(coordinator, id);
    EXPECT_EQ(result.records.size(), 1U);
    EXPECT_EQ(result.sources[1].state, SourceState::cancelled);
    EXPECT_TRUE(result.cancellation_requested);
    EXPECT_EQ(std::get<SessionSnapshot>(coordinator.cancel(id)).state, SessionState::cancelled);
}

TEST(Coordinator, DuplicateCallbackCompletesOnce) {
    auto a = std::make_shared<ControlledProvider>("a");
    SearchCoordinator coordinator(registry({a}));
    const auto id = submit(coordinator); const auto call = a->wait_call();
    call.complete({{record("a", "first")}, {}, true});
    call.complete({{record("a", "second")}, ProviderError::failed, false});
    const auto result = finished(coordinator, id);
    ASSERT_EQ(result.records.size(), 1U);
    EXPECT_EQ(result.records[0].record.source_record_id, "first");
    EXPECT_EQ(result.state, SessionState::completed);
}

TEST(Coordinator, ExpiryReclaimsSessionsAndLateCallbackCannotRestoreThem) {
    auto a = std::make_shared<ControlledProvider>("a");
    CoordinatorOptions options; options.max_sessions = 1; options.session_ttl = std::chrono::milliseconds(100);
    ManualClock clock;
    SearchCoordinator coordinator(registry({a}), options, [&] { return clock.now(); });
    const auto id = submit(coordinator); const auto call = a->wait_call();
    EXPECT_EQ(std::get<SearchError>(coordinator.submit(request())).code, "session_limit_reached");
    clock.milliseconds = 100; coordinator.sweep();
    EXPECT_EQ(std::get<SearchError>(coordinator.snapshot(id)).http_status, 410);
    EXPECT_EQ(std::get<SearchError>(coordinator.cancel(id)).code, "session_expired");
    await([&] { return call.stop.stop_requested(); });
    call.complete({{record("a")}, {}, true});
    EXPECT_EQ(std::get<SearchError>(coordinator.snapshot(id)).http_status, 410);
    const auto next = submit(coordinator);
    a->wait_call(1).complete({{}, {}, true});
    EXPECT_EQ(finished(coordinator, next).state, SessionState::completed);
    clock.milliseconds = 201; coordinator.sweep();
    EXPECT_EQ(std::get<SearchError>(coordinator.snapshot(id)).http_status, 404);
}

TEST(Coordinator, QuotasAndOrderingAreIndependentOfCallbackOrder) {
    for (bool reverse : {false, true}) {
        auto a = std::make_shared<ControlledProvider>("a"); auto b = std::make_shared<ControlledProvider>("b");
        CoordinatorOptions options; options.max_records_per_session = 3;
        SearchCoordinator coordinator(registry({b, a}), options);
        const auto id = submit(coordinator);
        const auto ca = a->wait_call(); const auto cb = b->wait_call();
        const ProviderResult ra{{record("a", "z"), record("a", "a"), record("a", "a"), record("a", "b")}, {}, true};
        const ProviderResult rb{{record("b", "z"), record("b", "a")}, {}, true};
        if (reverse) { cb.complete(rb); ca.complete(ra); } else { ca.complete(ra); cb.complete(rb); }
        const auto result = finished(coordinator, id);
        ASSERT_EQ(result.records.size(), 3U);
        EXPECT_EQ(result.records[0].record.source_id, "a");
        EXPECT_EQ(result.records[0].record.source_record_id, "a");
        EXPECT_EQ(result.records[1].record.source_record_id, "b");
        EXPECT_EQ(result.records[2].record.source_id, "b");
        EXPECT_EQ(result.records[2].record.source_record_id, "a");
        EXPECT_EQ(result.records[0].record.title, "A library record");
        EXPECT_EQ(result.records[0].original.title, "  A   library\nrecord  ");
        EXPECT_TRUE(result.sources[0].result_limit_reached);
    }
}

TEST(Coordinator, RejectsOversizedAndInvalidProviderResponses) {
    auto a = std::make_shared<ControlledProvider>("a");
    SearchCoordinator coordinator(registry({a}));
    auto query = request(); query.max_results_per_source = 1;
    const auto id = submit(coordinator, query);
    a->wait_call().complete({{record("a", "1"), record("a", "2")}, {}, true});
    EXPECT_EQ(finished(coordinator, id).sources[0].state, SourceState::failed);
    const auto next = submit(coordinator);
    auto oversized = record("a"); oversized.title = std::string(8193, 'a');
    a->wait_call(1).complete({{record("wrong-source"), oversized}, {}, true});
    auto result = finished(coordinator, next);
    EXPECT_EQ(result.sources[0].records_rejected, 2U);
    EXPECT_EQ(result.sources[0].state, SourceState::failed);
    EXPECT_TRUE(result.records.empty());
}

TEST(Coordinator, ProviderExceptionsAreSanitizedAndDisabledSourcesStayDisabled) {
    auto a = std::make_shared<ControlledProvider>("a"); a->throws = true;
    auto planned = source_catalog();
    SearchCoordinator coordinator(registry({a}, planned));
    const auto id = submit(coordinator);
    const auto result = finished(coordinator, id);
    EXPECT_EQ(result.sources.front().error->message, "Provider request failed.");
    EXPECT_EQ(result.sources[1].state, SourceState::external_only);
    EXPECT_EQ(result.sources[2].state, SourceState::disabled);
}

TEST(Coordinator, ShutdownRequestsStopAndLateCallbacksAreSafe) {
    auto a = std::make_shared<ControlledProvider>("a");
    ControlledProvider::Call call;
    {
        SearchCoordinator coordinator(registry({a}));
        (void)submit(coordinator); call = a->wait_call();
    }
    EXPECT_TRUE(call.stop.stop_requested());
    EXPECT_NO_THROW(call.complete({{record("a")}, {}, true}));
}

TEST(Coordinator, SimultaneousSubmissionsRespectSessionLimitsAndIdsAreDistinct) {
    auto a = std::make_shared<ControlledProvider>("a");
    CoordinatorOptions options; options.max_sessions = 8;
    SearchCoordinator coordinator(registry({a}), options);
    std::vector<SessionResult> results(16);
    std::vector<std::jthread> clients;
    for (std::size_t i = 0; i < results.size(); ++i) clients.emplace_back([&, i] { results[i] = coordinator.submit(request()); });
    clients.clear();
    std::set<std::string> ids;
    std::size_t rejected = 0;
    for (const auto& result : results) {
        if (const auto* session = std::get_if<SessionSnapshot>(&result)) {
            EXPECT_EQ(session->id.size(), 32U); ids.insert(session->id);
        } else { ++rejected; EXPECT_EQ(std::get<SearchError>(result).code, "session_limit_reached"); }
    }
    EXPECT_EQ(ids.size(), 8U); EXPECT_EQ(rejected, 8U);
}

TEST(Registry, RejectsDuplicateAndUnbackedOperationalSources) {
    auto a = std::make_shared<ControlledProvider>("a");
    EXPECT_THROW(ProviderRegistry({a->source()}), std::invalid_argument);
    EXPECT_THROW(ProviderRegistry({a->source(), a->source()}, {a}), std::invalid_argument);
    EXPECT_THROW(ProviderRegistry({a->source()}, {a, a}), std::invalid_argument);
    auto disabled = a->source(); disabled.implementation_status = ImplementationStatus::temporarily_unavailable;
    auto reg = std::make_shared<ProviderRegistry>(std::vector<Source>{disabled}, std::vector<std::shared_ptr<SearchProvider>>{a});
    EXPECT_FALSE(reg->search_available());
    SearchCoordinator coordinator(reg);
    EXPECT_EQ(std::get<SearchError>(coordinator.submit(request())).http_status, 503);
}

TEST(Coordinator, CompletionReceivedBeforeDeadlineSurvivesDelayedProcessing) {
    auto a = std::make_shared<ControlledProvider>("a");
    ManualClock clock;
    CoordinatorOptions options; options.provider_timeout = std::chrono::milliseconds(10);
    SearchCoordinator coordinator(registry({a}), options, [&] { return clock.now(); });
    const auto id = submit(coordinator); const auto call = a->wait_call();
    clock.milliseconds = 9;
    call.complete({{record("a")}, {}, true});
    clock.milliseconds = 11;
    coordinator.sweep();
    EXPECT_EQ(finished(coordinator, id).sources[0].state, SourceState::completed);
}

TEST(Coordinator, CancellingQueuedWorkPreventsProviderInvocation) {
    auto a = std::make_shared<ControlledProvider>("a");
    CoordinatorOptions options; options.max_concurrent_providers = 1;
    SearchCoordinator coordinator(registry({a}), options);
    const auto running = submit(coordinator); const auto call = a->wait_call();
    const auto queued = submit(coordinator);
    EXPECT_EQ(std::get<SessionSnapshot>(coordinator.cancel(queued)).state, SessionState::cancelled);
    call.complete({{}, {}, true});
    EXPECT_EQ(finished(coordinator, running).state, SessionState::completed);
    EXPECT_EQ(a->call_count(), 1U);
}
