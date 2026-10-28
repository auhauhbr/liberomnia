#include "liberomnia/source_catalog.hpp"

namespace liberomnia {

const std::vector<Source>& source_catalog() {
    static const std::vector<Source> sources{
        {"google-books", "Google Books", IntegrationType::api, ImplementationStatus::planned,
         {{"general"}, {"title"}, {"author"}, {"subject"}, {"language"}},
         "Metadata search is planned. Full-text and edition coverage require provider validation."},
        {"internet-archive", "Internet Archive", IntegrationType::api, ImplementationStatus::planned,
         {{"general"}, {"title"}, {"author"}, {"subject"}, {"publicationRange"}},
         "Metadata search is planned. Text availability and access rights vary by item."},
        {"open-library", "Open Library", IntegrationType::api, ImplementationStatus::planned,
         {{"general"}, {"title"}, {"author"}, {"subject"}, {"language"}, {"publicationRange"}},
         "Work and edition metadata are planned. No passage-search capability is asserted."},
        {"gallica", "Gallica", IntegrationType::api, ImplementationStatus::planned,
         {{"general"}, {"title"}, {"author"}, {"subject"}, {"passage"}},
         "Catalog and OCR-based passage search are planned; coverage must be validated per collection."},
        {"annas-archive", "Anna's Archive", IntegrationType::external, ImplementationStatus::planned,
         {{"externalSearch"}},
         "External search only is planned. No scraping or automation of protected pages."},
    };
    return sources;
}
} // namespace liberomnia
