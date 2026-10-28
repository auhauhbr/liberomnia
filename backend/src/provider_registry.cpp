#include "liberomnia/provider_registry.hpp"

#include <stdexcept>

namespace liberomnia::search {
ProviderRegistry::ProviderRegistry(std::vector<Source> sources,
                                  std::vector<std::shared_ptr<SearchProvider>> adapters) {
    for (auto& source : sources) {
        const auto id = source.id;
        domain::SearchFilters filters;
        filters.source_ids = {id};
        if (!filters.is_valid() || !entries_.emplace(id, RegisteredProvider{std::move(source), {}, {}}).second) {
            throw std::invalid_argument("Invalid or duplicate source descriptor");
        }
    }
    for (auto& adapter : adapters) {
        if (!adapter) throw std::invalid_argument("Null search provider");
        const auto found = entries_.find(adapter->source().id);
        if (found == entries_.end() || found->second.adapter ||
            found->second.source.integration_type != IntegrationType::api) {
            throw std::invalid_argument("Provider must match a unique API source");
        }
        found->second.capabilities = adapter->capabilities();
        found->second.adapter = std::move(adapter);
    }
    for (const auto& [id, entry] : entries_) {
        (void)id;
        if (entry.source.implementation_status == ImplementationStatus::available &&
            entry.source.integration_type == IntegrationType::api && !entry.adapter) {
            throw std::invalid_argument("Available API source requires an adapter");
        }
    }
}

const std::map<std::string, RegisteredProvider>& ProviderRegistry::entries() const noexcept { return entries_; }

bool ProviderRegistry::search_available() const noexcept {
    for (const auto& [id, entry] : entries_) {
        (void)id;
        if (entry.adapter && !entry.capabilities.fields.empty() &&
            entry.source.implementation_status == ImplementationStatus::available) return true;
    }
    return false;
}
} // namespace liberomnia::search
