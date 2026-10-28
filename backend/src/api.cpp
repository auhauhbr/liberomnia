#include "liberomnia/api.hpp"
#include "liberomnia/source_catalog.hpp"

#include <functional>

namespace liberomnia::http {

drogon::HttpResponsePtr health_response() {
    Json::Value body;
    body["status"] = "ok";
    body["application"] = "LiberOmnia";
    body["apiVersion"] = "1";
    return drogon::HttpResponse::newHttpJsonResponse(body);
}

drogon::HttpResponsePtr sources_response() {
    Json::Value body;
    body["apiVersion"] = "1";
    body["searchAvailable"] = false;
    body["sources"] = Json::Value(Json::arrayValue);
    for (const auto& source : source_catalog()) {
        Json::Value item;
        item["id"] = source.id;
        item["name"] = source.name;
        item["integrationType"] = source.integration_type == IntegrationType::api ? "api" : "external";
        item["implementationStatus"] = source.implementation_status == ImplementationStatus::available
                                           ? "available" : "planned";
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

void configure_application(drogon::HttpAppFramework& app, const Config& config) {
    app.addListener(config.address, config.port)
        .setThreadNum(2)
        .setFileTypes({})
        .setUploadPath((config.data_directory / "runtime").string())
        .setClientMaxBodySize(16 * 1024)
        .setClientMaxMemoryBodySize(16 * 1024);

    app.registerPostHandlingAdvice(
        [origin = config.frontend_origin](const drogon::HttpRequestPtr& request,
                                          const drogon::HttpResponsePtr& response) {
            response->addHeader("X-Content-Type-Options", "nosniff");
            response->addHeader("Cache-Control", "no-store");
            response->addHeader("Vary", "Origin");
            if (!origin.empty() && request->getHeader("Origin") == origin) {
                response->addHeader("Access-Control-Allow-Origin", origin);
                response->addHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
            }
        });

    using Callback = std::function<void(const drogon::HttpResponsePtr&)>;
    app.registerHandler("/api/health",
        [](const drogon::HttpRequestPtr&, Callback&& callback) { callback(health_response()); },
        {drogon::Get});
    app.registerHandler("/api/sources",
        [](const drogon::HttpRequestPtr&, Callback&& callback) { callback(sources_response()); },
        {drogon::Get});
}
} // namespace liberomnia::http
