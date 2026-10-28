#pragma once

#include "liberomnia/config.hpp"

#include <drogon/HttpAppFramework.h>
#include <drogon/HttpResponse.h>

namespace liberomnia::http {

[[nodiscard]] drogon::HttpResponsePtr health_response();
[[nodiscard]] drogon::HttpResponsePtr sources_response(const search::ProviderRegistry& registry);
void configure_application(drogon::HttpAppFramework& app, const Config& config,
                           std::shared_ptr<search::SearchCoordinator> coordinator);

} // namespace liberomnia::http
