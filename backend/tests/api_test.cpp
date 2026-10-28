#include "liberomnia/api.hpp"
#include "liberomnia/source_catalog.hpp"

#include <gtest/gtest.h>
#include <set>

TEST(Api, HealthReturnsVersionedJson) {
    const auto response = liberomnia::http::health_response();
    EXPECT_EQ(response->statusCode(), drogon::k200OK);
    EXPECT_EQ(response->contentType(), drogon::CT_APPLICATION_JSON);
    const auto body = response->getJsonObject();
    ASSERT_NE(body, nullptr);
    EXPECT_EQ((*body)["status"].asString(), "ok");
    EXPECT_EQ((*body)["application"].asString(), "LiberOmnia");
    EXPECT_EQ((*body)["apiVersion"].asString(), "1");
}

TEST(Api, CatalogContainsDistinctPlannedSourcesAndCapabilities) {
    const auto response = liberomnia::http::sources_response(liberomnia::search::ProviderRegistry(liberomnia::source_catalog()));
    EXPECT_EQ(response->statusCode(), drogon::k200OK);
    const auto body = response->getJsonObject();
    ASSERT_NE(body, nullptr);
    EXPECT_EQ((*body)["apiVersion"].asString(), "1");
    EXPECT_FALSE((*body)["searchAvailable"].asBool());
    const auto& sources = (*body)["sources"];
    ASSERT_TRUE(sources.isArray());
    ASSERT_EQ(sources.size(), 5U);
    std::set<std::string> ids;
    for (const auto& source : sources) {
        EXPECT_TRUE(ids.insert(source["id"].asString()).second);
        EXPECT_FALSE(source["name"].asString().empty());
        EXPECT_FALSE(source["notes"].asString().empty());
        EXPECT_EQ(source["implementationStatus"].asString(), "planned");
        ASSERT_TRUE(source["capabilities"].isArray());
        EXPECT_FALSE(source["capabilities"].empty());
        for (const auto& capability : source["capabilities"]) {
            EXPECT_FALSE(capability["id"].asString().empty());
            EXPECT_EQ(capability["status"].asString(), "planned");
        }
        EXPECT_EQ(source["integrationType"].asString(),
            source["id"].asString() == "annas-archive" ? "external" : "api");
    }
    EXPECT_EQ(ids, (std::set<std::string>{"google-books", "internet-archive", "open-library", "gallica", "annas-archive"}));
}
