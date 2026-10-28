#pragma once

#include "liberomnia/search_provider.hpp"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace liberomnia::search {

struct RegisteredProvider {
    Source source;
    std::shared_ptr<SearchProvider> adapter;
    ProviderCapabilities capabilities;
};

// Immutable after construction. Descriptors and adapters are explicit dependencies.
class ProviderRegistry {
public:
    explicit ProviderRegistry(std::vector<Source> sources,
                              std::vector<std::shared_ptr<SearchProvider>> adapters = {});
    [[nodiscard]] const std::map<std::string, RegisteredProvider>& entries() const noexcept;
    [[nodiscard]] bool search_available() const noexcept;
private:
    std::map<std::string, RegisteredProvider> entries_;
};

} // namespace liberomnia::search
