#include "liberomnia/google_books_provider.hpp"
#include "liberomnia/search_coordinator.hpp"
#include "liberomnia/api.hpp"
#include <gtest/gtest.h>
#include <condition_variable>
#include <future>
#include <mutex>
#include <random>

using namespace liberomnia;
using namespace liberomnia::search;
namespace {
class Client final : public BooksHttpClient {
public:
    struct Call { std::string query; std::chrono::milliseconds timeout; std::size_t bytes; std::stop_token stop; Callback callback; };
    std::mutex mutex;
    std::condition_variable changed;
    std::vector<Call> calls;
    void get(std::string query, std::string, std::chrono::milliseconds timeout, std::size_t bytes,
             std::stop_token stop, Callback callback) override {
        std::lock_guard lock(mutex);
        calls.push_back({std::move(query), timeout, bytes, stop, std::move(callback)}); changed.notify_all();
    }
    Call call(std::size_t i = 0) {
        std::unique_lock lock(mutex);
        if (!changed.wait_for(lock, std::chrono::seconds(2), [&] { return calls.size() > i; })) throw std::runtime_error("Missing HTTP call");
        return calls[i];
    }
};
Json::Value volume(std::string id = "volume-1", std::string date = "1847") {
    Json::Value v;
    v["id"] = id; v["volumeInfo"]["title"] = "Fixture history";
    v["volumeInfo"]["authors"].append("Fixture Author");
    if (!date.empty()) v["volumeInfo"]["publishedDate"] = date;
    v["volumeInfo"]["language"] = "la";
    v["volumeInfo"]["canonicalVolumeLink"] = "https://books.google.com/books?id=" + id;
    return v;
}
BooksHttpResponse page(std::vector<Json::Value> volumes = {}, int total = -1) {
    Json::Value root;
    root["totalItems"] = total < 0 ? static_cast<int>(volumes.size()) : total;
    if (!volumes.empty()) for (auto& v : volumes) root["items"].append(v);
    return {200, Json::writeString(Json::StreamWriterBuilder{}, root), {}, {}};
}
GoogleBooksOptions configured_options(GoogleBooksOptions options = {}) {
    // An opaque, ephemeral value is consumed only by the controlled transport.
    options.api_key = std::to_string(std::random_device{}());
    return options;
}
struct Harness {
    std::shared_ptr<Client> client = std::make_shared<Client>();
    std::shared_ptr<BooksRateLimiter> limiter = std::make_shared<BooksRateLimiter>();
    std::shared_ptr<GoogleBooksProvider> provider;
    domain::SearchRequest request;
    std::stop_source stop;
    std::optional<ProviderResult> result;
    int completions{};
    explicit Harness(GoogleBooksOptions options = {}) : provider(std::make_shared<GoogleBooksProvider>(client, configured_options(options), limiter)) { request.text = "Fixture"; }
    void start() { provider->search(request, stop.get_token(), [this](ProviderResult r) { result = std::move(r); ++completions; }); }
};
}
TEST(GoogleBooks, BuildsAllModesUnicodeAndLanguageWithoutInventedDateParameters) {
    domain::SearchRequest r; r.text = "São Tomás"; r.language = "pt"; r.publication = {1801,1900};
    for (auto [field, expected] : std::vector<std::pair<domain::SearchField,std::string>>{
        {domain::SearchField::general,"q=S%C3%A3o%20Tom%C3%A1s"},
        {domain::SearchField::title,"q=intitle%3A%22S%C3%A3o%20Tom%C3%A1s%22"},
        {domain::SearchField::author,"q=inauthor%3A%22S%C3%A3o%20Tom%C3%A1s%22"},
        {domain::SearchField::subject,"q=subject%3A%22S%C3%A3o%20Tom%C3%A1s%22"},
        {domain::SearchField::passage,"q=%22S%C3%A3o%20Tom%C3%A1s%22"}}) {
        r.field = field;
        EXPECT_EQ(GoogleBooksProvider::query(r), expected + "&printType=books&langRestrict=pt");
    }
    r.field = domain::SearchField::general; r.text = "a&key=x+#?"; r.language.reset();
    EXPECT_EQ(GoogleBooksProvider::query(r), "q=a%26key%3Dx%2B%23%3F&printType=books");
}
TEST(GoogleBooks, AllowsSupportedLanguagesAndRejectsUnvalidatedCodesWithoutNetwork) {
    for (const auto* language : {"pt","en","fr","la","es","it","de"}) {
        Harness h; h.request.language = language; h.start(); h.client->call().callback(page());
        ASSERT_TRUE(h.result); EXPECT_FALSE(h.result->error);
    }
    for (const auto* language : {"zz","eng","pt-BR","EN"}) {
        Harness h; h.request.language = language; h.start();
        ASSERT_TRUE(h.result); EXPECT_EQ(h.result->error, ProviderError::invalid_request); EXPECT_TRUE(h.client->calls.empty());
    }
}
TEST(GoogleBooks, StrictDatesIncludePartialAndLeapYears) {
    for (const auto* date : {"1847","1847-05","1847-05-21"}) EXPECT_EQ(GoogleBooksProvider::publication_year(date), 1847);
    EXPECT_EQ(GoogleBooksProvider::publication_year("2000-02-29"),2000);
    for (const auto* date : {"","184","0000","1847-00","1847-13","1847-02-29","1900-02-29","1847-04-31","1847-5","1847?","c.1847","1847-05-00","1847-05-21T00:00:00Z"})
        EXPECT_FALSE(GoogleBooksProvider::publication_year(date)) << date;
}
TEST(GoogleBooks, PreservesEditionIdentityMetadataAndAbsence) {
    auto v = volume(); v["volumeInfo"]["subtitle"] = "Subtitle"; v["volumeInfo"]["publisher"] = "Publisher";
    v["volumeInfo"]["industryIdentifiers"][0]["type"] = "OTHER";
    v["volumeInfo"]["industryIdentifiers"][0]["identifier"] = "source-id";
    v["volumeInfo"]["description"] = "Originally published in 1750";
    auto r = GoogleBooksProvider::record(v); ASSERT_TRUE(r);
    EXPECT_EQ(r->publication_year,1847); EXPECT_EQ(r->published_date,"1847"); EXPECT_EQ(r->language,"la");
    EXPECT_EQ(r->source_record_id,"volume-1"); EXPECT_EQ(r->subtitle,"Subtitle"); EXPECT_EQ(r->publisher,"Publisher");
    EXPECT_FALSE(r->work_id); EXPECT_FALSE(r->edition_id); EXPECT_FALSE(r->original_publication_year);
    ASSERT_EQ(r->identifiers.size(),1U); EXPECT_EQ(r->identifiers[0].value,"source-id");
    v["volumeInfo"].removeMember("authors"); v["volumeInfo"].removeMember("publishedDate");
    r = GoogleBooksProvider::record(v); ASSERT_TRUE(r); EXPECT_TRUE(r->authors.empty()); EXPECT_FALSE(r->publication_year);
}
TEST(GoogleBooks, HandlesNullWrongTypesLargeValuesAndMissingTitle) {
    for (auto value : {Json::Value(), Json::Value(42), Json::Value(Json::arrayValue), Json::Value("bad")}) EXPECT_FALSE(GoogleBooksProvider::record(value));
    auto v = volume(); v["volumeInfo"]["title"] = Json::Value(); EXPECT_FALSE(GoogleBooksProvider::record(v));
    v = volume(); v["volumeInfo"]["title"] = std::string(9000,'x'); EXPECT_FALSE(GoogleBooksProvider::record(v));
    v = volume(); v["volumeInfo"]["authors"] = 12; v["volumeInfo"]["publisher"] = Json::Value(Json::arrayValue);
    v["volumeInfo"]["industryIdentifiers"].append(100); v["accessInfo"] = "invalid";
    auto r = GoogleBooksProvider::record(v); ASSERT_TRUE(r); EXPECT_TRUE(r->authors.empty()); EXPECT_FALSE(r->publisher);
    v["volumeInfo"]["publishedDate"] = "1847?"; r = GoogleBooksProvider::record(v); ASSERT_TRUE(r); EXPECT_FALSE(r->publication_year);
}
TEST(GoogleBooks, FullPreviewDoesNotImplyDownloadsAndPartialIsDistinct) {
    auto v = volume(); v["accessInfo"]["viewability"] = "ALL_PAGES"; v["accessInfo"]["publicDomain"] = true;
    v["accessInfo"]["country"] = "BR"; v["volumeInfo"]["previewLink"] = "https://books.google.com/preview";
    auto r = GoogleBooksProvider::record(v); ASSERT_TRUE(r); ASSERT_EQ(r->access.size(),1U);
    EXPECT_EQ(r->access[0].availability,domain::AccessAvailability::viewable); EXPECT_EQ(r->access[0].viewability,"ALL_PAGES");
    EXPECT_EQ(r->public_domain,true); EXPECT_EQ(r->access_country,"BR"); EXPECT_FALSE(r->pdf_available);
    v["accessInfo"]["viewability"] = "PARTIAL"; r = GoogleBooksProvider::record(v); EXPECT_EQ(r->access[0].viewability,"PARTIAL");
    v["accessInfo"]["viewability"] = "NO_PAGES"; r = GoogleBooksProvider::record(v); EXPECT_EQ(r->access[0].availability,domain::AccessAvailability::metadata_only);
}
TEST(GoogleBooks, DownloadsRequireAvailableAndExplicitSafeLinkAndTokensAreRestricted) {
    for (const auto* format : {"pdf","epub"}) {
        auto v = volume(); v["accessInfo"][format]["isAvailable"] = true;
        auto r = GoogleBooksProvider::record(v); ASSERT_TRUE(r); EXPECT_EQ(r->access.size(),1U);
        v["accessInfo"][format]["downloadLink"] = "https://books.google.com/download";
        r = GoogleBooksProvider::record(v); ASSERT_EQ(r->access.size(),2U);
        EXPECT_EQ(r->access[1].availability,std::string(format)=="pdf" ? domain::AccessAvailability::pdf : domain::AccessAvailability::epub);
        v["accessInfo"][format]["isAvailable"] = false; r = GoogleBooksProvider::record(v); EXPECT_EQ(r->access.size(),1U);
        v["accessInfo"][format]["isAvailable"] = true; v["accessInfo"][format]["acsTokenLink"] = "https://books.google.com/token";
        r = GoogleBooksProvider::record(v); ASSERT_EQ(r->access.size(),2U); EXPECT_EQ(r->access[1].availability,domain::AccessAvailability::restricted);
        EXPECT_FALSE(r->access[1].url);
        v["accessInfo"][format].removeMember("acsTokenLink"); v["accessInfo"][format]["downloadLink"] = "javascript:alert(1)";
        r = GoogleBooksProvider::record(v); EXPECT_EQ(r->access.size(),1U);
    }
}
TEST(GoogleBooks, SnippetIsOnlySourceExcerptAndHasNoInventedLocation) {
    auto v = volume(); v["searchInfo"]["textSnippet"] = "<b>quoted words</b><script>alert(1)</script>";
    auto r = GoogleBooksProvider::record(v); ASSERT_TRUE(r); ASSERT_EQ(r->textual_evidence.size(),1U);
    EXPECT_EQ(r->textual_evidence[0].kind,domain::EvidenceKind::source_excerpt); EXPECT_FALSE(r->textual_evidence[0].location);
}
TEST(GoogleBooks, GeneralSearchAndEmptyWithoutItemsCompleteOnce) {
    Harness h; h.start(); auto call = h.client->call(); call.callback(page({volume()})); call.callback(page());
    ASSERT_TRUE(h.result); EXPECT_EQ(h.completions,1); EXPECT_EQ(h.result->records.size(),1U); EXPECT_FALSE(h.result->exhaustive);
    Harness empty; empty.start(); empty.client->call().callback(page()); ASSERT_TRUE(empty.result); EXPECT_FALSE(empty.result->error); EXPECT_TRUE(empty.result->records.empty());
}
TEST(GoogleBooks, MapsHttpAndTransportFailuresWithoutLeakingUpstreamBody) {
    for (auto [status,error] : std::vector<std::pair<int,ProviderError>>{{403,ProviderError::blocked},{401,ProviderError::authentication},{429,ProviderError::rate_limited},{500,ProviderError::failed},{302,ProviderError::invalid_response},{200,ProviderError::invalid_response}}) {
        Harness h; h.start(); h.client->call().callback({status,"private upstream details",{}, {}});
        ASSERT_TRUE(h.result); EXPECT_EQ(h.result->error,error); EXPECT_EQ(h.completions,1);
    }
    for (auto error : {ProviderError::network,ProviderError::timed_out}) {
        Harness h; h.start(); h.client->call().callback({0,{}, {},error}); EXPECT_EQ(h.result->error,error);
    }
    Harness h; h.start(); h.client->call().callback({403,R"({"error":{"errors":[{"reason":"rateLimitExceeded"}]}})",{}, {}});
    EXPECT_EQ(h.result->error,ProviderError::rate_limited); EXPECT_FALSE(h.limiter->ready());
}
TEST(GoogleBooks, RejectsInvalidJsonStructureAndOversizedResponse) {
    for (const auto* body : {"[]","null",R"({"totalItems":-1})",R"({"totalItems":0,"items":null})",R"({"totalItems":1})",R"({"totalItems":0,"totalItems":0})",R"({"totalItems":0,"error":{}})"}) {
        Harness h; h.start(); h.client->call().callback({200,body,{}, {}}); ASSERT_TRUE(h.result); EXPECT_EQ(h.result->error,ProviderError::invalid_response);
    }
    Harness deep; deep.start();
    std::string nested = "0";
    for (int i=0; i<40; ++i) nested = "[" + nested + "]";
    EXPECT_NO_THROW(deep.client->call().callback({200,"{\"totalItems\":0,\"extra\":"+nested+"}",{}, {}}));
    ASSERT_TRUE(deep.result); EXPECT_EQ(deep.result->error,ProviderError::invalid_response);
    Harness h; h.start(); h.client->call().callback({200,std::string(1048577,'x'),{}, {}}); EXPECT_EQ(h.result->error,ProviderError::invalid_response);
}
TEST(GoogleBooks, CancellationAndLateCallbackSurviveProviderDestruction) {
    Harness h; h.start(); auto callback = h.client->call().callback; h.provider.reset(); h.stop.request_stop();
    ASSERT_TRUE(h.result); EXPECT_EQ(h.result->error,ProviderError::cancelled); callback(page({volume()})); EXPECT_EQ(h.completions,1);
    Harness stopped; stopped.stop.request_stop(); stopped.start(); EXPECT_EQ(stopped.completions,1); EXPECT_TRUE(stopped.client->calls.empty());
}
TEST(GoogleBooks, LimitsPaginationCallBudgetAndResults) {
    GoogleBooksOptions options; options.page_size = 1; options.max_pages = 2; options.call_budget = 2;
    Harness h(options); h.start(); h.client->call().callback(page({volume("one")},100));
    EXPECT_NE(h.client->call(1).query.find("startIndex=1&maxResults=1"),std::string::npos);
    h.client->call(1).callback(page({volume("two")},100)); ASSERT_TRUE(h.result);
    EXPECT_EQ(h.client->calls.size(),2U); EXPECT_EQ(h.result->records.size(),2U); EXPECT_TRUE(h.result->result_limit_reached);
}
TEST(GoogleBooks, OneFallbackVerifiesTitleAuthorSubjectAndRejectsMissingFields) {
    for (auto field : {domain::SearchField::title,domain::SearchField::author,domain::SearchField::subject}) {
        Harness h; h.request.field = field; h.request.text = "Fixture"; h.start(); h.client->call().callback(page());
        ASSERT_EQ(h.client->calls.size(),2U); EXPECT_EQ(h.client->call(1).query,"q=Fixture&printType=books&startIndex=0&maxResults=20");
        auto match = volume("match"); match["volumeInfo"]["categories"].append("Fixture history");
        auto wrong = volume("wrong"); wrong["volumeInfo"]["title"] = "Unrelated"; wrong["volumeInfo"].removeMember("authors");
        auto substring = volume("substring"); substring["volumeInfo"]["title"] = "Fixtures"; substring["volumeInfo"].removeMember("authors");
        h.client->call(1).callback(page({match,wrong,substring})); ASSERT_TRUE(h.result);
        EXPECT_TRUE(h.result->fallback_used); EXPECT_FALSE(h.result->exhaustive); ASSERT_EQ(h.result->records.size(),1U);
        EXPECT_EQ(h.result->records[0].source_record_id,"match"); EXPECT_TRUE(h.result->records[0].textual_evidence.empty());
    }
}
TEST(GoogleBooks, NoFallbackOnErrorsNonemptyNativeOrExhaustedBudget) {
    Harness h; h.request.field = domain::SearchField::title; h.start(); h.client->call().callback(page({volume()}));
    EXPECT_FALSE(h.result->fallback_used); EXPECT_EQ(h.client->calls.size(),1U);
    GoogleBooksOptions options; options.call_budget = 1; Harness limited(options); limited.request.field = domain::SearchField::author;
    limited.start(); limited.client->call().callback(page()); EXPECT_FALSE(limited.result->fallback_used); EXPECT_EQ(limited.client->calls.size(),1U);
}
TEST(GoogleBooks, PartialRecordsSurviveLaterFailureAndDuplicateIdsRemainDistinct) {
    GoogleBooksOptions options; options.page_size = 2;
    Harness h(options); h.start(); h.client->call().callback(page({volume("one"),volume("two")},100));
    h.client->call(1).callback({500,{}, {}, {}}); ASSERT_TRUE(h.result);
    EXPECT_EQ(h.result->records.size(),2U); EXPECT_EQ(h.result->error,ProviderError::failed);
    Harness duplicate; duplicate.start(); duplicate.client->call().callback(page({volume("one"),volume("one"),volume("two")}));
    EXPECT_EQ(duplicate.result->records.size(),2U);
}
TEST(GoogleBooks, SharedRateLimiterHonorsConcurrencyMinuteAndRetryAfter) {
    auto now = std::chrono::steady_clock::time_point{};
    auto limiter = std::make_shared<BooksRateLimiter>(1,2,[&] { return now; });
    EXPECT_TRUE(limiter->acquire()); EXPECT_FALSE(limiter->acquire()); limiter->release("120"); EXPECT_FALSE(limiter->acquire());
    now += std::chrono::seconds(119); EXPECT_FALSE(limiter->acquire()); now += std::chrono::seconds(1); EXPECT_TRUE(limiter->acquire());
    limiter->release(); EXPECT_TRUE(limiter->acquire()); limiter->release(); EXPECT_FALSE(limiter->acquire());
    limiter->defer("Wed, 01 Jan 2031 00:00:00 GMT"); EXPECT_FALSE(limiter->ready());
    Harness a, b; b.provider = std::make_shared<GoogleBooksProvider>(b.client,configured_options(),a.limiter);
    a.start(); a.client->call().callback({429,{},"120",{}}); b.start(); ASSERT_TRUE(b.result); EXPECT_EQ(b.result->error,ProviderError::rate_limited); EXPECT_TRUE(b.client->calls.empty());
}
TEST(GoogleBooks, ReadinessUpdatesCatalogAndPreservesPlannedProviders) {
    Harness h; auto sources = source_catalog(); for (auto& source : sources) if (source.id == "google-books") source = h.provider->source();
    ProviderRegistry registry(sources,{h.provider}); EXPECT_TRUE(registry.search_available());
    EXPECT_TRUE(h.client->calls.empty());
    EXPECT_EQ(h.provider->operational_status()->state,"unverified");
    h.start(); EXPECT_TRUE(registry.search_available());
    auto body = http::sources_response(registry)->getJsonObject();
    for (const auto& source : (*body)["sources"]) EXPECT_EQ(source["implementationStatus"].asString(),source["id"] == "google-books" ? "available" : "planned");
    h.client->call().callback(page()); EXPECT_TRUE(registry.search_available());
}
TEST(GoogleBooks, CoordinatorFiltersEditionDatesAcrossArbitraryCenturies) {
    for (auto [from,to] : std::vector<std::pair<int,int>>{{1501,1600},{1701,1800},{1801,1900},{1901,2000}}) {
        Harness h; auto registry = std::make_shared<ProviderRegistry>(std::vector<Source>{h.provider->source()},std::vector<std::shared_ptr<SearchProvider>>{h.provider});
        SearchCoordinator coordinator(registry); h.request.publication = {from,to};
        auto initial = std::get<SessionSnapshot>(coordinator.submit(h.request));
        auto call = h.client->call(); EXPECT_EQ(call.query.find("1801"),std::string::npos);
        auto v = volume("inside", std::to_string(from)+"-05"); v["volumeInfo"]["description"] = "Originally published in 1750";
        call.callback(page({v,volume("outside",std::to_string(to+1)),volume("unknown","")}));
        SessionSnapshot final;
        // A bounded blocking wait in this test only, not in the HTTP implementation.
        for (int i=0;i<100;++i) { final=std::get<SessionSnapshot>(coordinator.snapshot(initial.id)); if(final.finished) break; std::this_thread::sleep_for(std::chrono::milliseconds(2)); }
        ASSERT_TRUE(final.finished); ASSERT_EQ(final.records.size(),1U); EXPECT_EQ(final.records[0].record.publication_year,from);
        EXPECT_TRUE(final.sources[0].local_filters_applied); EXPECT_TRUE(final.sources[0].coverage_incomplete);
    }
}

TEST(GoogleBooks, RacingCancellationAndTransportCallbacksCompleteOnce) {
    for (int i = 0; i < 30; ++i) {
        Harness h; h.start(); auto callback = h.client->call().callback;
        std::jthread first([callback] { callback(page({volume()})); });
        std::jthread second([callback] { callback(page()); });
        std::jthread cancel([&h] { h.stop.request_stop(); });
        first.join(); second.join(); cancel.join();
        EXPECT_EQ(h.completions,1);
    }
}
TEST(GoogleBooks, InvalidKeyIsAuthenticationAndDisablesCatalogReadiness) {
    Harness h; h.start();
    h.client->call().callback({400,R"({"error":{"details":[{"reason":"API_KEY_INVALID"}]}})",{}, {}});
    EXPECT_EQ(h.result->error,ProviderError::key_invalid);
    EXPECT_EQ(h.provider->implementation_status(),ImplementationStatus::temporarily_unavailable);
}

TEST(GoogleBooksOperations, StartupIsUnverifiedAndNeverMakesAnExternalRequest) {
    Harness h;
    EXPECT_TRUE(h.client->calls.empty());
    auto status = *h.provider->operational_status();
    EXPECT_TRUE(status.implemented); EXPECT_TRUE(status.configured); EXPECT_TRUE(status.search_allowed);
    EXPECT_EQ(status.state,"unverified"); EXPECT_FALSE(status.last_successful_at);
    h.start(); h.client->call().callback(page());
    status = *h.provider->operational_status();
    EXPECT_EQ(status.state,"available"); EXPECT_TRUE(status.last_successful_at); EXPECT_FALSE(status.error);
}
TEST(GoogleBooksOperations, MissingConfigurationAndDisabledNeverSendRequests) {
    for (bool enabled : {false,true}) {
        auto client = std::make_shared<Client>(); GoogleBooksOptions options; options.enabled = enabled;
        GoogleBooksProvider provider(client, options);
        auto status = *provider.operational_status();
        EXPECT_EQ(status.state,enabled ? "configuration_required" : "disabled");
        EXPECT_TRUE(status.implemented); EXPECT_FALSE(status.configured); EXPECT_FALSE(status.search_allowed);
        domain::SearchRequest request; request.text = "Fixture";
        int completions = 0;
        provider.search(request,{},[&](ProviderResult r){ ++completions; EXPECT_EQ(r.error,ProviderError::configuration_missing); });
        EXPECT_EQ(completions,1); EXPECT_TRUE(client->calls.empty());
    }
}
namespace {
BooksHttpResponse failure(int code, std::string reason, std::string limit = {}, std::string value = {}) {
    Json::Value root;
    auto& e = root["error"]; e["code"] = code;
    e["status"] = code == 429 ? "RESOURCE_EXHAUSTED" : "PERMISSION_DENIED";
    e["details"][0]["reason"] = reason;
    auto& meta = e["details"][0]["metadata"];
    meta["quota_limit"] = limit;
    if (!value.empty()) meta["quota_limit_value"] = value;
    return {code,Json::writeString(Json::StreamWriterBuilder{},root),{}, {}};
}
}
TEST(GoogleBooksOperations, TypedAdministrativeErrorsLatchWithoutRetryingOrPublishingMetadata) {
    const std::vector<std::pair<BooksHttpResponse,ProviderError>> cases{
        {failure(400,"API_KEY_INVALID"),ProviderError::key_invalid},
        {failure(400,""),ProviderError::blocked},
        {{400,"{}",{},{}},ProviderError::configuration_invalid},
        {{401,"{}",{},{}},ProviderError::authentication},
        {{403,"{}",{},{}},ProviderError::blocked},
        {failure(403,"SERVICE_DISABLED"),ProviderError::api_disabled},
        {failure(403,"API_KEY_HTTP_REFERRER_BLOCKED"),ProviderError::key_restricted},
        {failure(403,"CREDENTIALS_MISSING"),ProviderError::configuration_missing},
        {failure(429,"QUOTA_EXCEEDED","QueriesPerDay","25"),ProviderError::quota_daily_exhausted},
        {failure(429,"QUOTA_EXCEEDED","QueriesPerDay","0"),ProviderError::quota_daily_zero},
        {{403,R"({"error":{"errors":[{"reason":"dailyLimitExceeded"}]}})",{},{}},ProviderError::quota_daily_exhausted},
        {{429,R"({"error":{"message":"Quota exceeded: daily limit: 0"}})",{},{}},ProviderError::quota_daily_zero},
    };
    for (const auto& [response,expected] : cases) {
        Harness h; h.start(); auto callback = h.client->call().callback; callback(response);
        ASSERT_TRUE(h.result); EXPECT_EQ(h.result->error,expected);
        EXPECT_FALSE(h.provider->operational_status()->search_allowed);
        for (int i=0;i<3;++i) h.start();
        EXPECT_EQ(h.client->calls.size(),1U); EXPECT_EQ(h.completions,4);
        callback(page()); EXPECT_EQ(h.completions,4);
        EXPECT_EQ(h.provider->operational_status()->error,expected);
    }
}
TEST(GoogleBooksOperations, TransientFailuresRecoverOnlyOnNewUserRequestAfterCooldown) {
    for (const auto& response : std::vector<BooksHttpResponse>{
        {429,"{}",{},{}}, failure(429,"RATE_LIMIT_EXCEEDED","QueriesPerMinute"),
        {500,"{}",{},{}}, {0,{}, {},ProviderError::network}, {0,{}, {},ProviderError::timed_out}, {200,"[]",{},{}},
    }) {
        auto now = std::chrono::steady_clock::time_point{};
        Harness h; h.limiter = std::make_shared<BooksRateLimiter>(4,30,[&]{return now;});
        h.provider = std::make_shared<GoogleBooksProvider>(h.client,configured_options(),h.limiter);
        h.start(); h.client->call().callback(response);
        ASSERT_TRUE(h.result->error); EXPECT_FALSE(h.provider->operational_status()->search_allowed);
        if (response.body.find("QueriesPerMinute") != std::string::npos) EXPECT_EQ(h.result->error,ProviderError::rate_limit_minute);
        h.start(); EXPECT_EQ(h.client->calls.size(),1U);
        now += std::chrono::minutes(2);
        EXPECT_TRUE(h.provider->operational_status()->search_allowed);
        EXPECT_EQ(h.client->calls.size(),1U); // Time and status reads never perform probes/retries.
        h.start(); h.client->call(1).callback(page());
        EXPECT_EQ(h.provider->operational_status()->state,"available"); EXPECT_FALSE(h.result->error);
    }
}
TEST(GoogleBooksOperations, RetryAfterNumericDateAbsentAndInvalidAreDeterministic) {
    for (auto [header,seconds] : std::vector<std::pair<std::string,int>>{
        {"120",120},{"Wed, 01 Jan 2025 00:02:00 GMT",120},{"",60},{"not a date",60},
    }) {
        auto now = std::chrono::steady_clock::time_point{};
        auto limiter = std::make_shared<BooksRateLimiter>(4,30,[&]{return now;},[]{return std::time_t(1735689600);});
        Harness h; h.provider = std::make_shared<GoogleBooksProvider>(h.client,configured_options(),limiter);
        h.start(); h.client->call().callback({429,"{}",header,{}});
        now += std::chrono::seconds(seconds-1); EXPECT_FALSE(h.provider->operational_status()->search_allowed);
        now += std::chrono::seconds(1); EXPECT_TRUE(h.provider->operational_status()->search_allowed);
        EXPECT_EQ(h.client->calls.size(),1U);
    }
}
TEST(GoogleBooksOperations, ConcurrentFailuresAndSuccessCannotEraseDailyQuotaBlock) {
    for (int i=0; i<20; ++i) {
        Harness h; std::atomic<int> completed{};
        for (int j=0;j<3;++j) h.provider->search(h.request,{},[&](ProviderResult){++completed;});
        auto a = h.client->call(0).callback, b = h.client->call(1).callback, c = h.client->call(2).callback;
        std::jthread first([a]{a(failure(429,"QUOTA_EXCEEDED","QueriesPerDay","0"));});
        std::jthread second([b]{b({500,"{}",{}, {}});});
        std::jthread third([c]{c(page());});
        first.join(); second.join(); third.join();
        EXPECT_EQ(completed,3); EXPECT_FALSE(h.provider->operational_status()->search_allowed);
        EXPECT_EQ(h.provider->operational_status()->error,ProviderError::quota_daily_zero);
    }
}
TEST(GoogleBooksOperations, CancelledLateErrorsDoNotPoisonOperationalState) {
    Harness h; h.start(); auto callback=h.client->call().callback; h.stop.request_stop();
    callback(failure(429,"QUOTA_EXCEEDED","QueriesPerDay","0"));
    EXPECT_EQ(h.completions,1); EXPECT_EQ(h.result->error,ProviderError::cancelled);
    EXPECT_EQ(h.provider->operational_status()->state,"unverified");
    EXPECT_TRUE(h.provider->operational_status()->search_allowed);
}
TEST(GoogleBooksOperations, CatalogRedactsSecretsAndInternalProjectMetadata) {
    Harness h;
    const auto secret = std::to_string(std::random_device{}());
    Json::Value body;
    body["error"]["message"] = secret;
    body["error"]["details"][0]["reason"] = "API_KEY_INVALID";
    body["error"]["details"][0]["metadata"]["consumer"] = secret;
    body["error"]["details"][0]["metadata"]["key"] = secret;
    h.start(); h.client->call().callback({400,Json::writeString(Json::StreamWriterBuilder{},body),{}, {}});
    ProviderRegistry registry({h.provider->source()},{h.provider});
    auto response = http::sources_response(registry)->getJsonObject();
    const auto serialized = Json::writeString(Json::StreamWriterBuilder{},*response);
    EXPECT_EQ(serialized.find(secret),std::string::npos);
    EXPECT_EQ(serialized.find("consumer"),std::string::npos);
    EXPECT_EQ((*response)["sources"][0]["operational"]["error"]["code"],"provider_key_invalid");
    EXPECT_FALSE((*response)["searchAvailable"].asBool());
}
TEST(GoogleBooksOperations, CorrectedConfigurationStartsUnverifiedWithoutProbeAndCanSearch) {
    Harness before; before.start(); before.client->call().callback(failure(400,"API_KEY_INVALID"));
    auto corrected = std::make_shared<GoogleBooksProvider>(before.client,configured_options(),before.limiter);
    EXPECT_TRUE(corrected->operational_status()->search_allowed);
    EXPECT_EQ(corrected->operational_status()->state,"unverified"); EXPECT_EQ(before.client->calls.size(),1U);
    bool complete=false;
    corrected->search(before.request,{},[&](ProviderResult r){complete=true;EXPECT_FALSE(r.error);});
    before.client->call(1).callback(page()); EXPECT_TRUE(complete);
    EXPECT_EQ(corrected->operational_status()->state,"available");
}

TEST(GoogleBooksOperations, QuotaEvidenceIsConservativeAndMalformedDetailsStaySafe) {
    for (const auto& [body,expected] : std::vector<std::pair<std::string,ProviderError>>{
        {R"({"error":{"details":[{"reason":"QUOTA_EXCEEDED","metadata":{"quota_unit":"1/d/{project}","quota_limit_value":0}}]}})",ProviderError::quota_daily_zero},
        {R"({"error":{"details":[{"metadata":{"quota_limit":"QueriesPerDay","quota_limit_value":"100"}},{"metadata":{"quota_limit":"QueriesPerMinute","quota_limit_value":"0"}}]}})",ProviderError::quota_daily_exhausted},
        {R"({"error":{"message":"Daily quota exhausted for project 0"}})",ProviderError::quota_daily_exhausted},
        {R"({"error":{"details":[{"violations":[{"quotaId":"QueriesPerDay","quotaValue":"0"}]}]}})",ProviderError::quota_daily_zero},
        {R"({"error":{"details":[null,[],{"reason":8,"metadata":false}],"errors":false,"message":[]}})",ProviderError::rate_limited},
        {R"({"error":{"status":"RESOURCE_EXHAUSTED","details":[{"reason":"QUOTA_EXCEEDED"}]}})",ProviderError::rate_limited},
    }) {
        Harness h; h.start(); h.client->call().callback({429,body,{}, {}});
        ASSERT_TRUE(h.result); EXPECT_EQ(h.result->error,expected);
    }
    Harness h; h.start();
    auto response = failure(429,"QUOTA_EXCEEDED","QueriesPerDay","0"); response.status = 200;
    h.client->call().callback(response); EXPECT_EQ(h.result->error,ProviderError::quota_daily_zero);
}
TEST(GoogleBooksOperations, DailyQuotaDoesNotSetInventedCooldownForCorrectedConfiguration) {
    Harness h; h.start(); auto response = failure(429,"QUOTA_EXCEEDED","QueriesPerDay","0"); response.retry_after = "30";
    h.client->call().callback(response);
    EXPECT_TRUE(h.limiter->ready());
    EXPECT_FALSE(h.provider->operational_status()->search_allowed);
    EXPECT_EQ(h.provider->operational_status()->state,"quota_exhausted");
}
