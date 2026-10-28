#include "liberomnia/config.hpp"

#include <gtest/gtest.h>
#include <map>
#include <stdexcept>

namespace {
liberomnia::Config load(const std::map<std::string, std::string>& values = {}) {
    return liberomnia::Config::from_environment([&values](std::string_view key) -> std::optional<std::string> {
        const auto found = values.find(std::string(key));
        return found == values.end() ? std::nullopt : std::optional(found->second);
    });
}

TEST(Config, DefaultsBindToLoopbackAndDoNotOpenDatabase) {
    const auto config = load();
    EXPECT_EQ(config.address, "127.0.0.1");
    EXPECT_EQ(config.port, 8080);
    EXPECT_EQ(config.database_path(), std::filesystem::path("./data/liberomnia.sqlite3"));
    EXPECT_EQ(config.frontend_origin, "http://localhost:5173");
}

TEST(Config, SupportsOverridesAndIpv6) {
    const auto config = load({{"LIBEROMNIA_ADDRESS", "::1"}, {"LIBEROMNIA_PORT", "9090"},
        {"LIBEROMNIA_DATA_DIR", "/tmp/library"}, {"LIBEROMNIA_FRONTEND_ORIGIN", "http://[::1]:5173"}});
    EXPECT_EQ(config.address, "::1");
    EXPECT_EQ(config.port, 9090);
    EXPECT_EQ(config.database_path(), "/tmp/library/liberomnia.sqlite3");
    EXPECT_EQ(config.frontend_origin, "http://[::1]:5173");
}

TEST(Config, RejectsInvalidPorts) {
    for (const auto* value : {"", "0", "65536", "-1", "8080extra", " 80", "999999999999999"}) {
        EXPECT_THROW(load({{"LIBEROMNIA_PORT", value}}), std::invalid_argument) << value;
    }
    EXPECT_EQ(load({{"LIBEROMNIA_PORT", "65535"}}).port, 65535);
}

TEST(Config, ValidatesSearchResourceLimits) {
    const auto config = load({{"LIBEROMNIA_SEARCH_CONCURRENCY", "2"}, {"LIBEROMNIA_SEARCH_TIMEOUT_MS", "1000"}});
    EXPECT_EQ(config.search.max_concurrent_providers, 2U);
    EXPECT_EQ(config.search.provider_timeout, std::chrono::milliseconds(1000));
    for (const auto* key : {"LIBEROMNIA_SEARCH_CONCURRENCY", "LIBEROMNIA_SEARCH_MAX_PENDING",
         "LIBEROMNIA_SEARCH_MAX_SESSIONS", "LIBEROMNIA_SEARCH_MAX_RECORDS", "LIBEROMNIA_SEARCH_TIMEOUT_MS",
         "LIBEROMNIA_SEARCH_SESSION_TTL_MS"}) {
        EXPECT_THROW(load({{key, "0"}}), std::invalid_argument);
        EXPECT_THROW(load({{key, "999999999999999"}}), std::invalid_argument);
        EXPECT_THROW(load({{key, "2x"}}), std::invalid_argument);
    }
}

TEST(Config, ValidatesGoogleBooksLimitsWithoutEmbeddingCredentials) {
    EXPECT_TRUE(load().google_books_enabled);
    EXPECT_TRUE(load().google_books.api_key.empty());
    EXPECT_FALSE(load({{"GOOGLE_BOOKS_ENABLED", "false"}}).google_books_enabled);
    EXPECT_THROW(load({{"GOOGLE_BOOKS_ENABLED", "yes"}}), std::invalid_argument);
    for (auto [key, maximum] : std::vector<std::pair<std::string,int>>{
        {"GOOGLE_BOOKS_PAGE_SIZE",40}, {"GOOGLE_BOOKS_MAX_PAGES",10}, {"GOOGLE_BOOKS_CALL_BUDGET",11},
        {"GOOGLE_BOOKS_TIMEOUT_MS",30000}, {"GOOGLE_BOOKS_MAX_RESPONSE_BYTES",4194304}}) {
        EXPECT_THROW(load({{key,"0"}}), std::invalid_argument);
        EXPECT_THROW(load({{key,std::to_string(maximum+1)}}), std::invalid_argument);
        EXPECT_NO_THROW(load({{key,std::to_string(maximum)}}));
    }
    EXPECT_THROW(load({{"GOOGLE_BOOKS_MAX_RESPONSE_BYTES","1023"}}), std::invalid_argument);
}

TEST(Config, RejectsInvalidAddressAndDataPath) {
    EXPECT_THROW(load({{"LIBEROMNIA_ADDRESS", "localhost"}}), std::invalid_argument);
    EXPECT_THROW(load({{"LIBEROMNIA_ADDRESS", "999.1.2.3"}}), std::invalid_argument);
    EXPECT_THROW(load({{"LIBEROMNIA_DATA_DIR", ""}}), std::invalid_argument);
}

TEST(Config, RejectsUnsafeOrigins) {
    for (const auto* value : {"*", "null", "https://example.org/", "https://user@example.org",
             "http://localhost:0", "http://localhost:99999", "http://[abc]", "http://localhost\r\nX: evil"}) {
        EXPECT_THROW(load({{"LIBEROMNIA_FRONTEND_ORIGIN", value}}), std::invalid_argument) << value;
    }
    EXPECT_TRUE(load({{"LIBEROMNIA_FRONTEND_ORIGIN", ""}}).frontend_origin.empty());
    EXPECT_EQ(load({{"LIBEROMNIA_FRONTEND_ORIGIN", "https://library.example"}}).frontend_origin,
              "https://library.example");
}
} // namespace
