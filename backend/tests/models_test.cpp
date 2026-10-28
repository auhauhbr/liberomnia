#include "liberomnia/models.hpp"
#include "liberomnia/search_provider.hpp"

#include <gtest/gtest.h>
#include <type_traits>

using namespace liberomnia::domain;

TEST(Models, PublicationRangeAllowsUnknownAndOpenBounds) {
    EXPECT_TRUE(PublicationRange{}.is_valid());
    EXPECT_TRUE((PublicationRange{1801, 1900}.is_valid()));
    EXPECT_TRUE((PublicationRange{1801, std::nullopt}.is_valid()));
    EXPECT_TRUE((PublicationRange{std::nullopt, 1900}.is_valid()));
    EXPECT_FALSE((PublicationRange{1900, 1801}.is_valid()));
    EXPECT_FALSE((PublicationRange{0, 1900}.is_valid()));
    EXPECT_FALSE((PublicationRange{1801, 10000}.is_valid()));
}

TEST(Models, QueryRequiresTextAndValidFilters) {
    SearchQuery query;
    EXPECT_FALSE(query.is_valid());
    query.text = "  \t\n";
    EXPECT_FALSE(query.is_valid());
    query.text = "Machado de Assis";
    query.publication = {1801, 1900};
    EXPECT_TRUE(query.is_valid());
    query.source_ids = {""};
    EXPECT_FALSE(query.is_valid());
    query.source_ids = {"gallica"};
    query.language = " ";
    EXPECT_FALSE(query.is_valid());
}

TEST(Models, RecordPreservesEditionAndSourceIdentity) {
    BibliographicRecord record;
    EXPECT_FALSE(record.is_valid());
    record.source_id = "open-library";
    record.source_record_id = "edition-id";
    record.title = "Dom Casmurro";
    record.edition = "First edition";
    record.publication_year = 1899;
    EXPECT_TRUE(record.is_valid());
    EXPECT_EQ(record.edition, "First edition");
    record.publication_year.reset();
    EXPECT_TRUE(record.is_valid());
    record.authors = {" "};
    EXPECT_FALSE(record.is_valid());
    record.authors.clear();
    record.publication_year = 0;
    EXPECT_FALSE(record.is_valid());
}

TEST(Models, ProviderInterfaceSupportsSafePolymorphicDestruction) {
    EXPECT_TRUE(std::has_virtual_destructor_v<liberomnia::search::SearchProvider>);
}
