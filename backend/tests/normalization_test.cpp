#include "liberomnia/normalization.hpp"
#include "test_provider.hpp"

#include <gtest/gtest.h>

using namespace liberomnia;

TEST(Normalization, NormalizesOptionalWhitespaceAndRetainsOriginalData) {
    auto record = test::record("a");
    record.edition = "  "; record.publisher = "  Library   Press  ";
    record.authors = {" Author  One ", " "};
    record.original_publication_year = 1801;
    record.volume = " II "; record.identifiers.push_back({" ISBN ", " 9780000000000 "});
    const auto result = search::normalize_record(record, "a");
    ASSERT_TRUE(result);
    EXPECT_EQ(result->record.title, "A library record");
    EXPECT_EQ(result->original.title, record.title);
    EXPECT_FALSE(result->record.edition);
    EXPECT_EQ(result->record.publisher, "Library Press");
    EXPECT_EQ(result->record.authors, std::vector<std::string>{"Author One"});
    EXPECT_EQ(result->record.original_publication_year, 1801);
    EXPECT_TRUE(result->record.access.empty());
    EXPECT_TRUE(result->record.textual_evidence.empty());
}

TEST(Normalization, RejectsInvalidProvenanceYearsAndUnsafeUrls) {
    auto record = test::record("a");
    EXPECT_FALSE(search::normalize_record(record, "b"));
    record.original_publication_year = 0;
    EXPECT_FALSE(search::normalize_record(record, "a"));
    record.original_publication_year.reset();
    for (const auto* url : {"javascript:alert(1)", "file:///tmp/private", "https://user:secret@example.org", "https://example.org/\nsecret"}) {
        record.original_url = url;
        EXPECT_FALSE(search::normalize_record(record, "a"));
    }
    record.original_url = "https://example.org/edition";
    EXPECT_TRUE(search::normalize_record(record, "a"));
    record.source_record_id = " id ";
    EXPECT_FALSE(search::normalize_record(record, "a"));
}
