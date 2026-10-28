#pragma once

#include <optional>
#include <string>
#include <vector>

namespace liberomnia::domain {

enum class SearchField { general, title, author, subject, passage };
enum class AccessAvailability { viewable, pdf, epub, restricted, metadata_only, unknown };
enum class EvidenceKind { confirmed_occurrence, source_excerpt, approximate_match, unverified };

struct TextualEvidence {
    EvidenceKind kind{EvidenceKind::unverified};
    std::optional<std::string> text;
    std::optional<std::string> location;
};
struct AccessInfo {
    AccessAvailability availability{AccessAvailability::unknown};
    std::optional<std::string> url;
    std::optional<std::string> viewability;
    std::optional<std::string> format;
};
struct BibliographicIdentifier {
    std::string scheme;
    std::string value;
};

struct PublicationRange {
    std::optional<int> from;
    std::optional<int> to;
    [[nodiscard]] bool is_valid() const noexcept;
};

struct SearchFilters {
    PublicationRange publication;
    std::optional<std::string> language;
    std::vector<std::string> source_ids;
    std::optional<AccessAvailability> availability;
    std::size_t max_results_per_source{20};
    [[nodiscard]] bool is_valid() const;
};

struct SearchRequest : SearchFilters {
    std::string text;
    SearchField field{SearchField::general};
    [[nodiscard]] bool is_valid() const;
};
using SearchQuery = SearchRequest;

// A source record may describe a work or an edition; these identifiers stay distinct.
// Cross-source identity is deliberately not inferred.
struct BibliographicRecord {
    std::string source_id;
    std::string source_record_id;
    std::string title;
    std::optional<std::string> subtitle;
    std::optional<std::string> published_date;
    std::optional<std::string> access_country;
    std::optional<bool> public_domain;
    std::optional<bool> pdf_available;
    std::optional<bool> epub_available;
    std::optional<std::string> preview_url;
    std::optional<std::string> info_url;
    std::vector<std::string> authors;
    std::optional<std::string> edition;
    std::optional<std::string> publisher;
    std::optional<int> publication_year;
    std::optional<std::string> language;
    std::optional<int> original_publication_year;
    std::optional<std::string> volume;
    std::vector<BibliographicIdentifier> identifiers;
    std::optional<std::string> original_url;
    std::optional<std::string> work_id;
    std::optional<std::string> edition_id;
    std::vector<AccessInfo> access;
    std::vector<TextualEvidence> textual_evidence;
    [[nodiscard]] bool is_valid() const;
};

} // namespace liberomnia::domain
