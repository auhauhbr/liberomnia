#pragma once

#include <string>
#include <vector>

namespace liberomnia {

enum class IntegrationType { api, external };
enum class ImplementationStatus { planned, available };
enum class CapabilityStatus { planned, confirmed };

struct SourceCapability {
    std::string id;
    CapabilityStatus status{CapabilityStatus::planned};
};

struct Source {
    std::string id;
    std::string name;
    IntegrationType integration_type{IntegrationType::api};
    ImplementationStatus implementation_status{ImplementationStatus::planned};
    std::vector<SourceCapability> capabilities;
    std::string notes;
};

[[nodiscard]] const std::vector<Source>& source_catalog();

} // namespace liberomnia
