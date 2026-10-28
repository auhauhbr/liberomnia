#pragma once

#include "liberomnia/models.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace liberomnia::search {

struct NormalizedRecord {
    domain::BibliographicRecord record;
    domain::BibliographicRecord original;
};

[[nodiscard]] std::string normalize_spaces(std::string_view value);
[[nodiscard]] std::optional<NormalizedRecord> normalize_record(domain::BibliographicRecord record,
                                                              std::string_view expected_source);

} // namespace liberomnia::search
