#include "liberomnia/normalization.hpp"

#include <algorithm>
#include <cctype>

namespace liberomnia::search {
namespace {
bool valid_url(const std::optional<std::string>& url) {
    return !url || ((url->starts_with("https://") || url->starts_with("http://")) &&
        url->size() <= 2048 && url->find_first_of("\r\n\t ") == std::string::npos &&
        url->find('@') == std::string::npos && url->size() > (url->starts_with("https://") ? 8U : 7U));
}
void optional_text(std::optional<std::string>& text) {
    if (text) {
        *text = normalize_spaces(*text);
        if (text->empty()) text.reset();
    }
}
} // namespace

std::string normalize_spaces(std::string_view value) {
    std::string result;
    bool space = false;
    for (const unsigned char c : value) {
        if (std::isspace(c)) { space = !result.empty(); continue; }
        if (space) result += ' ';
        space = false;
        result += static_cast<char>(c);
    }
    return result;
}

std::optional<NormalizedRecord> normalize_record(domain::BibliographicRecord record, std::string_view expected_source) {
    if (record.source_id != expected_source || record.source_record_id.empty() || record.source_record_id.size() > 256 ||
        normalize_spaces(record.source_record_id) != record.source_record_id || record.authors.size() > 64 ||
        record.identifiers.size() > 32 || record.access.size() > 8 || record.textual_evidence.size() > 8) return std::nullopt;
    std::size_t bytes = 0;
    const auto bounded = [&bytes](const std::string& text) {
        bytes += text.size();
        return text.size() <= 8192 && bytes <= 32768 &&
            std::none_of(text.begin(), text.end(), [](unsigned char c) { return c == 0 || (c < 32 && !std::isspace(c)); });
    };
    const auto optional_bounded = [&bounded](const auto& text) { return !text || bounded(*text); };
    if (!optional_bounded(record.subtitle) || !optional_bounded(record.published_date) ||
        !optional_bounded(record.access_country) || !valid_url(record.preview_url) || !valid_url(record.info_url) ||
        !optional_bounded(record.preview_url) || !optional_bounded(record.info_url) || !bounded(record.source_id) || !bounded(record.source_record_id) || !bounded(record.title) ||
        !std::all_of(record.authors.begin(), record.authors.end(), bounded) ||
        !optional_bounded(record.edition) || !optional_bounded(record.publisher) ||
        !optional_bounded(record.language) || !optional_bounded(record.volume) ||
        !optional_bounded(record.original_url) || !valid_url(record.original_url) ||
        !optional_bounded(record.work_id) || !optional_bounded(record.edition_id)) return std::nullopt;
    for (const auto& id : record.identifiers) if (!bounded(id.scheme) || !bounded(id.value)) return std::nullopt;
    for (const auto& access : record.access) {
        if (!optional_bounded(access.viewability) || !optional_bounded(access.format) || !optional_bounded(access.url) || !valid_url(access.url) ||
            access.availability < domain::AccessAvailability::viewable || access.availability > domain::AccessAvailability::unknown)
            return std::nullopt;
    }
    for (const auto& evidence : record.textual_evidence) {
        if (!optional_bounded(evidence.text) || !optional_bounded(evidence.location) ||
            evidence.kind < domain::EvidenceKind::confirmed_occurrence || evidence.kind > domain::EvidenceKind::unverified)
            return std::nullopt;
    }
    auto original = record;
    record.title = normalize_spaces(record.title);
    for (auto& author : record.authors) author = normalize_spaces(author);
    std::erase_if(record.authors, [](const auto& author) { return author.empty(); });
    optional_text(record.edition);
    optional_text(record.publisher);
    optional_text(record.language);
    optional_text(record.volume);
    optional_text(record.work_id);
    optional_text(record.edition_id);
    for (auto& id : record.identifiers) { id.scheme = normalize_spaces(id.scheme); id.value = normalize_spaces(id.value); }
    for (auto& evidence : record.textual_evidence) { optional_text(evidence.text); optional_text(evidence.location); }
    if (!record.is_valid()) return std::nullopt;
    return NormalizedRecord{std::move(record), std::move(original)};
}
} // namespace liberomnia::search
