#include "liberomnia/api.hpp"
#include "liberomnia/source_catalog.hpp"
#include "liberomnia/search_api.hpp"

#include <functional>
#include <regex>

namespace liberomnia::http {
namespace {
void response_headers(const drogon::HttpRequestPtr& request, const drogon::HttpResponsePtr& response,
                      const std::string& origin) {
    response->addHeader("X-Content-Type-Options", "nosniff");
    response->addHeader("Cache-Control", "no-store");
    response->addHeader("Vary", "Origin");
    if (!origin.empty() && request->getHeader("Origin") == origin) {
        response->addHeader("Access-Control-Allow-Origin", origin);
        response->addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        response->addHeader("Access-Control-Allow-Headers", "Content-Type");
    }
}
} // namespace

drogon::HttpResponsePtr health_response() {
    Json::Value body;
    body["status"] = "ok";
    body["application"] = "LiberOmnia";
    body["apiVersion"] = "1";
    return drogon::HttpResponse::newHttpJsonResponse(body);
}

drogon::HttpResponsePtr sources_response(const search::ProviderRegistry& registry) {
    Json::Value body;
    body["apiVersion"] = "1";
    body["searchAvailable"] = registry.search_available();
    body["sources"] = Json::Value(Json::arrayValue);
    for (const auto& [id, entry] : registry.entries()) {
        (void)id;
        const auto& source = entry.source;
        Json::Value item;
        item["id"] = source.id;
        item["name"] = source.name;
        item["integrationType"] = source.integration_type == IntegrationType::api ? "api" : "external";
        item["implementationStatus"] = source.implementation_status == ImplementationStatus::available
                                           ? "available" : source.implementation_status == ImplementationStatus::temporarily_unavailable
                                           ? "temporarily_unavailable" : "planned";
        item["filterSupport"]["publication"] = std::string(search::name(entry.capabilities.publication));
        item["filterSupport"]["language"] = std::string(search::name(entry.capabilities.language));
        item["filterSupport"]["availability"] = std::string(search::name(entry.capabilities.availability));
        item["notes"] = source.notes;
        item["capabilities"] = Json::Value(Json::arrayValue);
        for (const auto& capability : source.capabilities) {
            Json::Value entry;
            entry["id"] = capability.id;
            entry["status"] = capability.status == CapabilityStatus::confirmed ? "confirmed" : "planned";
            item["capabilities"].append(entry);
        }
        body["sources"].append(item);
    }
    return drogon::HttpResponse::newHttpJsonResponse(body);
}

void configure_application(drogon::HttpAppFramework& app, const Config& config,
                           std::shared_ptr<search::SearchCoordinator> coordinator) {
    app.addListener(config.address, config.port)
        .setThreadNum(2)
        .setFileTypes({})
        .setUploadPath((config.data_directory / "runtime").string())
        .setClientMaxBodySize(16 * 1024)
        .setClientMaxMemoryBodySize(16 * 1024);

    app.registerPostHandlingAdvice(
        [origin = config.frontend_origin](const drogon::HttpRequestPtr& request,
                                          const drogon::HttpResponsePtr& response) {
            response_headers(request, response, origin);
        });

    // Handle preflight before Drogon's automatic OPTIONS response generation.
    app.registerPreRoutingAdvice([origin = config.frontend_origin](const drogon::HttpRequestPtr& request,
        drogon::AdviceCallback&& callback, drogon::AdviceChainCallback&& next) {
        if (request->method() != drogon::Options || !request->path().starts_with("/api/")) { next(); return; }
        const auto& path = request->path();
        static const std::regex session_path(R"(^/api/search/[a-f0-9]{32}(/results)?$)");
        static const std::regex cancel_path(R"(^/api/search/[a-f0-9]{32}/cancel$)");
        const bool post = path == "/api/search" || std::regex_match(path, cancel_path);
        const bool get = path == "/api/health" || path == "/api/sources" || std::regex_match(path, session_path);
        const auto method = request->getHeader("Access-Control-Request-Method");
        auto headers = request->getHeader("Access-Control-Request-Headers");
        for (auto& c : headers) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
        const bool allowed = !origin.empty() && request->getHeader("Origin") == origin &&
            ((get && method == "GET") || (post && method == "POST")) &&
            (headers.empty() || headers == "content-type");
        auto response = allowed ? drogon::HttpResponse::newHttpResponse()
            : search_response(search::SearchError{"invalid_preflight", "Preflight request is not allowed.", 403});
        if (allowed) response->setStatusCode(drogon::k204NoContent);
        response_headers(request, response, origin);
        response->addHeader("Vary", "Origin, Access-Control-Request-Method, Access-Control-Request-Headers");
        callback(response);
    });

    using Callback = std::function<void(const drogon::HttpResponsePtr&)>;
    app.registerHandler("/api/health",
        [](const drogon::HttpRequestPtr&, Callback&& callback) { callback(health_response()); },
        {drogon::Get});
    app.registerHandler("/api/sources",
        [coordinator](const drogon::HttpRequestPtr&, Callback&& callback) { callback(sources_response(coordinator->registry())); },
        {drogon::Get});
    register_search_routes(app, std::move(coordinator), config.frontend_origin);
}
} // namespace liberomnia::http
