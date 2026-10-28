#include "liberomnia/models.hpp"

#include <algorithm>
#include <cctype>

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

bool SearchQuery::is_valid() const {
    return has_text(text) && publication.is_valid() && (!language || has_text(*language)) &&
           std::all_of(source_ids.begin(), source_ids.end(), has_text);
}

bool BibliographicRecord::is_valid() const {
    return has_text(source_id) && has_text(source_record_id) && has_text(title) &&
           valid_year(publication_year) && (!language || has_text(*language)) &&
           (!edition || has_text(*edition)) && (!publisher || has_text(*publisher)) &&
           std::all_of(authors.begin(), authors.end(), has_text);
}
} // namespace liberomnia::domain
