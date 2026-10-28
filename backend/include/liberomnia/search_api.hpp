#pragma once

#include "liberomnia/search_coordinator.hpp"

#include <drogon/HttpAppFramework.h>
#include <drogon/HttpResponse.h>

namespace liberomnia::http {

using ParsedSearch = std::variant<domain::SearchRequest, search::SearchError>;
[[nodiscard]] ParsedSearch parse_search_request(std::string_view body);
[[nodiscard]] drogon::HttpResponsePtr search_response(const search::SessionResult& result,
                                                     drogon::HttpStatusCode success = drogon::k200OK,
                                                     bool include_records = false);
void register_search_routes(drogon::HttpAppFramework& app,
                            std::shared_ptr<search::SearchCoordinator> coordinator,
                            std::string frontend_origin);

} // namespace liberomnia::http
