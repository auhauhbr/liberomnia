#include "liberomnia/models.hpp"

#include <algorithm>
#include <cctype>
#include <set>
#include <regex>

namespace liberomnia::domain {
namespace {
bool has_text(const std::string& value) {
    return std::any_of(value.begin(), value.end(), [](unsigned char character) {
        return !std::isspace(character);
    });
}

bool valid_year(const std::optional<int>& year) {
    return !year || (*year >= 1 && *year <= 9999);
}
} // namespace

bool PublicationRange::is_valid() const noexcept {
    return valid_year(from) && valid_year(to) && (!from || !to || *from <= *to);
}

bool SearchFilters::is_valid() const {
    const auto valid_id = [](const std::string& id) {
        return !id.empty() && id.size() <= 64 && std::all_of(id.begin(), id.end(), [](unsigned char c) {
            return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
        });
    };
    static const std::regex language_tag("^[A-Za-z]{2,8}(-[A-Za-z0-9]{1,8})*$");
    const bool valid_language = !language || (language->size() <= 35 && std::regex_match(*language, language_tag));
    const bool valid_availability = !availability || (*availability >= AccessAvailability::viewable &&
        *availability <= AccessAvailability::unknown);
    return publication.is_valid() && valid_language && valid_availability && max_results_per_source >= 1 &&
        max_results_per_source <= 100 && source_ids.size() <= 16 &&
        std::all_of(source_ids.begin(), source_ids.end(), valid_id) &&
        std::set<std::string>(source_ids.begin(), source_ids.end()).size() == source_ids.size();
}

bool SearchRequest::is_valid() const {
    return SearchFilters::is_valid() && has_text(text) && text.size() <= 4096 &&
        std::none_of(text.begin(), text.end(), [](unsigned char c) {
            return c == 0 || (c < 32 && c != '\t' && c != '\n' && c != '\r');
        }) && field >= SearchField::general && field <= SearchField::passage;
}

bool BibliographicRecord::is_valid() const {
    return has_text(source_id) && has_text(source_record_id) && has_text(title) &&
           valid_year(publication_year) && valid_year(original_publication_year) && (!language || has_text(*language)) &&
           (!edition || has_text(*edition)) && (!publisher || has_text(*publisher)) &&
           (!volume || has_text(*volume)) &&
           (!work_id || has_text(*work_id)) && (!edition_id || has_text(*edition_id)) &&
           std::all_of(identifiers.begin(), identifiers.end(), [](const auto& identifier) {
               return has_text(identifier.scheme) && has_text(identifier.value);
           }) &&
           std::all_of(authors.begin(), authors.end(), has_text);
}
} // namespace liberomnia::domain
