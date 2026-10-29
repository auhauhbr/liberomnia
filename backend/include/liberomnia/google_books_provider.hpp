#pragma once

#include "liberomnia/search_provider.hpp"
#include "liberomnia/google_books_options.hpp"
#include <atomic>
#include <chrono>
#include <ctime>
#include <memory>
#include <json/json.h>

namespace liberomnia::search {

struct BooksHttpResponse {
    int status{};
    std::string body;
    std::string retry_after;
    std::optional<ProviderError> error;
};
// Transport must bound the body during reception, return promptly, and complete once.
class BooksHttpClient {
public:
    using Callback = std::function<void(BooksHttpResponse)>;
    virtual ~BooksHttpClient() = default;
    virtual void get(std::string query, std::string api_key, std::chrono::milliseconds timeout,
                     std::size_t max_bytes, std::stop_token stop, Callback callback) = 0;
};
[[nodiscard]] std::shared_ptr<BooksHttpClient> make_books_http_client();

// Shared across all searches and, by default, all adapters in the process.
class BooksRateLimiter {
public:
    using Clock = std::function<std::chrono::steady_clock::time_point()>;
    using WallClock = std::function<std::time_t()>;
    explicit BooksRateLimiter(std::size_t concurrency = 4, std::size_t per_minute = 30,
                              Clock now = std::chrono::steady_clock::now,
                              WallClock wall_now = [] { return std::time(nullptr); });
    bool acquire();
    void release(std::string_view retry_after = {});
    void defer(std::string_view retry_after);
    bool ready() const;
private:
    struct State;
    std::shared_ptr<State> state_;
};

struct BooksOperationalState;
class GoogleBooksProvider final : public SearchProvider {
public:
    explicit GoogleBooksProvider(std::shared_ptr<BooksHttpClient> client, GoogleBooksOptions options = {},
                                 std::shared_ptr<BooksRateLimiter> limiter = {});
    const Source& source() const noexcept override;
    ProviderCapabilities capabilities() const override;
    ImplementationStatus implementation_status() const noexcept override;
    std::optional<ProviderOperationalStatus> operational_status() const override;
    void search(domain::SearchRequest request, std::stop_token stop, Completion completion) override;
    static std::optional<int> publication_year(std::string_view date);
    static std::optional<domain::BibliographicRecord> record(const Json::Value& item);
    static std::string query(const domain::SearchRequest& request, bool fallback = false);
private:
    std::shared_ptr<BooksHttpClient> client_;
    GoogleBooksOptions options_;
    std::shared_ptr<BooksRateLimiter> limiter_;
    Source source_;
    std::shared_ptr<BooksOperationalState> state_;
};
} // namespace liberomnia::search
