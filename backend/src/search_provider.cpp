#include "liberomnia/search_provider.hpp"

namespace liberomnia::search {
std::string_view provider_error_code(ProviderError error) {
    switch (error) {
        case ProviderError::configuration_missing: return "provider_configuration_missing";
        case ProviderError::configuration_invalid: return "provider_configuration_invalid";
        case ProviderError::key_invalid: return "provider_key_invalid";
        case ProviderError::api_disabled: return "provider_api_disabled";
        case ProviderError::key_restricted: return "provider_key_restricted";
        case ProviderError::quota_daily_exhausted: return "provider_daily_quota_exhausted";
        case ProviderError::quota_daily_zero: return "provider_daily_quota_zero";
        case ProviderError::rate_limit_minute: return "provider_minute_rate_limit";
        case ProviderError::rate_limited: return "provider_rate_limited";
        case ProviderError::blocked: return "provider_blocked";
        case ProviderError::network: return "provider_network_error";
        case ProviderError::authentication: return "provider_authentication";
        case ProviderError::invalid_response: return "invalid_provider_response";
        case ProviderError::invalid_request: return "provider_invalid_request";
        case ProviderError::timed_out: return "provider_timed_out";
        case ProviderError::cancelled: return "provider_cancelled";
        case ProviderError::failed: return "provider_failed";
    }
    return "provider_failed";
}
std::string_view provider_error_message(ProviderError error) {
    switch (error) {
        case ProviderError::configuration_missing: return "Provider application identification is missing. Administrator configuration is required.";
        case ProviderError::configuration_invalid: return "Provider rejected the configuration. Administrator review is required.";
        case ProviderError::key_invalid: return "Provider rejected the API key. Administrator intervention is required.";
        case ProviderError::api_disabled: return "The API is not enabled for this configuration. Administrator intervention is required.";
        case ProviderError::key_restricted: return "API key restrictions do not permit this request. Administrator intervention is required.";
        case ProviderError::quota_daily_exhausted: return "Provider daily quota is exhausted. An administrator must verify quota availability before resuming. Reset time is unknown.";
        case ProviderError::quota_daily_zero: return "Provider reports a daily quota of zero. An administrator must review the project quota. Waiting a few minutes is not a confirmed remedy.";
        case ProviderError::rate_limit_minute: return "Provider per-minute limit reached. Requests are temporarily paused.";
        case ProviderError::rate_limited: return "Provider rate limit reached. The quota period was not confirmed. Requests are temporarily paused.";
        case ProviderError::blocked: return "Provider denied access. Administrator review is required.";
        case ProviderError::network: return "Unable to reach provider. A later user request may try again.";
        case ProviderError::authentication: return "Provider credentials were rejected. Administrator intervention is required.";
        case ProviderError::invalid_response: return "Provider returned an invalid response.";
        case ProviderError::invalid_request: return "Provider does not accept this language or query.";
        case ProviderError::timed_out: return "Provider deadline exceeded.";
        case ProviderError::cancelled: return "Provider search cancelled.";
        case ProviderError::failed: return "Provider request failed.";
    }
    return "Provider request failed.";
}
} // namespace liberomnia::search
