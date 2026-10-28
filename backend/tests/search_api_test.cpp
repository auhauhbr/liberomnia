#include "liberomnia/search_api.hpp"
#include "liberomnia/source_catalog.hpp"

#include <gtest/gtest.h>

using namespace liberomnia;

TEST(SearchApi, ParsesAllModesAndInclusiveNineteenthCentury) {
    for (const auto* field : {"general", "title", "author", "subject", "passage"}) {
        const std::string body = std::string(R"({"text":"research","field":")") + field +
            R"(","publication":{"from":1801,"to":1900},"language":"pt-BR","sourceIds":["gallica"],"maxResultsPerSource":100,"availability":"pdf"})";
        const auto parsed = http::parse_search_request(body);
        ASSERT_TRUE(std::holds_alternative<domain::SearchRequest>(parsed));
        const auto query = std::get<domain::SearchRequest>(parsed);
        EXPECT_EQ(query.publication.from, 1801);
        EXPECT_EQ(query.publication.to, 1900);
        EXPECT_EQ(query.max_results_per_source, 100U);
        EXPECT_EQ(query.availability, domain::AccessAvailability::pdf);
    }
}

TEST(SearchApi, StrictlyRejectsMalformedJsonTypesUnknownKeysAndLimits) {
    for (const auto* body : {
        R"({"text":"a","text":"b"})", "not-json", R"({"text":"a",})", R"({"text":"a"} {})"
    }) {
        const auto parsed = http::parse_search_request(body);
        ASSERT_TRUE(std::holds_alternative<search::SearchError>(parsed)) << body;
        EXPECT_EQ(std::get<search::SearchError>(parsed).http_status, 400);
    }
    for (const auto* body : {
        "[]", "null", "{}", R"({"text":"  "})", R"({"text":123})", R"({"text":"a","field":"unknown"})",
        R"({"text":"a","publication":{"from":1900,"to":1801}})", R"({"text":"a","publication":{"from":0}})",
        R"({"text":"a","publication":{"from":1.0}})", R"({"text":"a","publication":{"from":true}})",
        R"({"text":"a","publication":{"to":10000}})", R"({"text":"a","publication":{"extra":1}})",
        R"({"text":"a","maxResultsPerSource":0})", R"({"text":"a","maxResultsPerSource":101})",
        R"({"text":"a","maxResultsPerSource":1.0})", R"({"text":"a","maxResultsPerSource":true})",
        R"({"text":"a","sourceIds":["gallica","gallica"]})", R"({"text":"a","sourceIds":[1]})",
        R"({"text":"a","sourceIds":"gallica"})", R"({"text":"a","url":"https://arbitrary.example"})",
        R"({"text":"a","language":"--"})", R"({"text":"a","availability":"free"})", R"({"text":"a\u0000b"})"
    }) {
        const auto parsed = http::parse_search_request(body);
        ASSERT_TRUE(std::holds_alternative<search::SearchError>(parsed)) << body;
        EXPECT_EQ(std::get<search::SearchError>(parsed).http_status, 422) << body;
    }
    EXPECT_EQ(std::get<search::SearchError>(http::parse_search_request(std::string(16385, ' '))).http_status, 413);
    const auto large = std::string("{\"text\":\"") + std::string(4097, 'a') + "\"}";
    EXPECT_EQ(std::get<search::SearchError>(http::parse_search_request(large)).http_status, 422);
}

TEST(SearchApi, ErrorsUseConsistentStatusAndEnvelopeWithoutPrivateDetails) {
    search::SearchCoordinator coordinator(std::make_shared<search::ProviderRegistry>(source_catalog()));
    domain::SearchRequest query; query.text = "research";
    const auto response = http::search_response(coordinator.submit(query), drogon::k202Accepted);
    EXPECT_EQ(response->statusCode(), drogon::k503ServiceUnavailable);
    const auto body = response->getJsonObject();
    ASSERT_NE(body, nullptr);
    EXPECT_EQ((*body)["apiVersion"].asString(), "1");
    EXPECT_EQ((*body)["error"]["code"].asString(), "no_available_providers");
    EXPECT_FALSE(body->isMember("session"));
}

TEST(SearchApi, SessionSerializationPreservesWorkEditionAccessAndEvidenceSeparately) {
    search::SessionSnapshot session;
    session.id = std::string(32, 'a');
    session.created_at = std::chrono::system_clock::time_point{};
    domain::BibliographicRecord record;
    record.source_id = "a"; record.source_record_id = "edition-2"; record.title = "Title";
    record.publication_year = 1899; record.original_publication_year = 1840;
    record.access.push_back({domain::AccessAvailability::metadata_only, {}});
    record.textual_evidence.push_back({domain::EvidenceKind::unverified, "excerpt", {}});
    session.records.push_back({record, record});
    auto response = http::search_response(session, drogon::k202Accepted, true);
    EXPECT_EQ(response->statusCode(), drogon::k202Accepted);
    EXPECT_EQ(response->getHeader("Location"), "/api/search/" + session.id);
    auto json = response->getJsonObject();
    EXPECT_EQ((*json)["session"]["createdAt"].asString(), "1970-01-01T00:00:00Z");
    const auto& result = (*json)["results"][0]["record"];
    EXPECT_EQ(result["publicationYear"].asInt(), 1899);
    EXPECT_EQ(result["originalPublicationYear"].asInt(), 1840);
    EXPECT_TRUE(result["originalUrl"].isNull());
    EXPECT_EQ(result["access"][0]["availability"].asString(), "metadata_only");
    EXPECT_EQ(result["textualEvidence"][0]["kind"].asString(), "unverified");
}
