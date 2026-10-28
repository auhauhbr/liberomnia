#include "liberomnia/api.hpp"
#include "liberomnia/google_books_provider.hpp"

// This executable exists only in BUILD_TESTING. It uses the real Google Books
// adapter and HTTP routes, but no external network or production fixture data.
class FixtureClient final : public liberomnia::search::BooksHttpClient {
public:
    void get(std::string query, std::string, std::chrono::milliseconds, std::size_t,
             std::stop_token stop, Callback callback) override {
        drogon::app().getLoop()->queueInLoop([query = std::move(query), stop, callback = std::move(callback)] {
            if (stop.stop_requested()) return callback({0, {}, {}, liberomnia::search::ProviderError::cancelled});
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
    auto provider = std::make_shared<liberomnia::search::GoogleBooksProvider>(std::make_shared<FixtureClient>());
    auto sources = liberomnia::source_catalog();
    for (auto& source : sources) if (source.id == provider->source().id) source = provider->source();
    auto registry = std::make_shared<liberomnia::search::ProviderRegistry>(std::move(sources),
        std::vector<std::shared_ptr<liberomnia::search::SearchProvider>>{provider});
    auto coordinator = std::make_shared<liberomnia::search::SearchCoordinator>(registry);
    liberomnia::http::configure_application(drogon::app(), config, coordinator);
    drogon::app().run();
}
