#pragma once

#include <optional>
#include <string>
#include <vector>

namespace liberomnia::domain {

enum class SearchField { general, title, author, passage };

struct PublicationRange {
    std::optional<int> from;
    std::optional<int> to;
    [[nodiscard]] bool is_valid() const noexcept;
};

struct SearchQuery {
    std::string text;
    SearchField field{SearchField::general};
    PublicationRange publication;
    std::optional<std::string> language;
    std::vector<std::string> source_ids;
    [[nodiscard]] bool is_valid() const;
};

// An edition from one source. Cross-source identity is deliberately not inferred.
struct BibliographicRecord {
    std::string source_id;
    std::string source_record_id;
    std::string title;
    std::vector<std::string> authors;
    std::optional<std::string> edition;
    std::optional<std::string> publisher;
    std::optional<int> publication_year;
    std::optional<std::string> language;
    [[nodiscard]] bool is_valid() const;
};

} // namespace liberomnia::domain
