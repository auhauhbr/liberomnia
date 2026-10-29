#include "liberomnia/api.hpp"
#include "liberomnia/google_books_provider.hpp"
#include <random>

// This executable exists only in BUILD_TESTING. It uses the real Google Books
// adapter and HTTP routes, but no external network or production fixture data.
class FixtureClient final : public liberomnia::search::BooksHttpClient {
public:
    std::atomic<int> calls{};
    void get(std::string query, std::string, std::chrono::milliseconds, std::size_t,
             std::stop_token stop, Callback callback) override {
        ++calls;
        drogon::app().getLoop()->queueInLoop([query = std::move(query), stop, callback = std::move(callback)] {
            if (stop.stop_requested()) return callback({0, {}, {}, liberomnia::search::ProviderError::cancelled});
            if (query.find("q=daily") != std::string::npos) return callback({429,
                R"({"error":{"code":429,"status":"RESOURCE_EXHAUSTED","details":[{"reason":"QUOTA_EXCEEDED","metadata":{"quota_limit":"QueriesPerDay","quota_limit_value":"0","consumer":"projects/private-fixture"}}]}})", {}, {}});
            if (query.find("q=quota") != std::string::npos) return callback({429, "{}", "1", {}});
            if (query.find("q=blocked") != std::string::npos) return callback({403, "{}", {}, {}});
            if (query.find("q=invalid") != std::string::npos) return callback({200, "[]", {}, {}});
            if (query.find("q=timeout") != std::string::npos) return callback({0, {}, {}, liberomnia::search::ProviderError::timed_out});
            if (query.find("intitle") != std::string::npos || query.find("inauthor") != std::string::npos)
                return callback({200, R"({"totalItems":0})", {}, {}});
            callback({200, R"({"totalItems":3,"items":[
              {"id":"fixture-1880","volumeInfo":{"title":"Fixture history","publishedDate":"1880-05","authors":["Fixture author"],"language":"la","canonicalVolumeLink":"https://books.google.com/books?id=fixture-1880"},"searchInfo":{"textSnippet":"<b>fixture excerpt</b>"},"accessInfo":{"viewability":"PARTIAL","pdf":{"isAvailable":true,"downloadLink":"https://books.google.com/fixture-download"}}},
              {"id":"fixture-1750","volumeInfo":{"title":"Fixture history","publishedDate":"1750"}},
              {"id":"fixture-undated","volumeInfo":{"title":"Fixture history"}}
            ]})", {}, {}});
        });
    }
};
int main() {
    const auto config = liberomnia::Config::from_environment();
    std::filesystem::create_directories(config.data_directory / "runtime");
    auto options = config.google_books;
    options.api_key = std::to_string(std::random_device{}());
    auto clock = std::make_shared<std::atomic<long>>(0);
    auto limiter = std::make_shared<liberomnia::search::BooksRateLimiter>(4,30,[clock] {
        return std::chrono::steady_clock::time_point{} + std::chrono::seconds(clock->load());
    });
    auto client = std::make_shared<FixtureClient>();
    auto provider = std::make_shared<liberomnia::search::GoogleBooksProvider>(client, options, limiter);
    drogon::app().registerHandler("/test/calls", [client](const drogon::HttpRequestPtr&, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
        Json::Value body; body["calls"] = client->calls.load();
        callback(drogon::HttpResponse::newHttpJsonResponse(body));
    }, {drogon::Get});
    drogon::app().registerHandler("/test/advance", [clock](const drogon::HttpRequestPtr&, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
        clock->fetch_add(60);
        callback(drogon::HttpResponse::newHttpJsonResponse(Json::Value(Json::objectValue)));
    }, {drogon::Post});
    auto sources = liberomnia::source_catalog();
    for (auto& source : sources) if (source.id == provider->source().id) source = provider->source();
    auto registry = std::make_shared<liberomnia::search::ProviderRegistry>(std::move(sources),
        std::vector<std::shared_ptr<liberomnia::search::SearchProvider>>{provider});
    auto coordinator = std::make_shared<liberomnia::search::SearchCoordinator>(registry);
    liberomnia::http::configure_application(drogon::app(), config, coordinator);
    drogon::app().run();
}
