#include "liberomnia/api.hpp"
#include "test_provider.hpp"

#include <atomic>
#include <map>

int main() {
    const auto config = liberomnia::Config::from_environment();
    std::filesystem::create_directories(config.data_directory / "runtime");
    std::map<std::string, std::shared_ptr<liberomnia::test::ControlledProvider>> controls;
    std::vector<liberomnia::Source> sources;
    std::vector<std::shared_ptr<liberomnia::search::SearchProvider>> adapters;
    for (const auto* id : {"fast", "slow"}) {
        auto provider = std::make_shared<liberomnia::test::ControlledProvider>(id);
        controls.emplace(id, provider);
        sources.push_back(provider->source()); adapters.push_back(provider);
    }
    auto clock = std::make_shared<std::atomic<std::int64_t>>(0);
    auto registry = std::make_shared<liberomnia::search::ProviderRegistry>(sources, adapters);
    auto options = config.search;
    options.max_sessions = 2;
    options.provider_timeout = std::chrono::milliseconds(100);
    options.session_ttl = std::chrono::milliseconds(1000);
    auto coordinator = std::make_shared<liberomnia::search::SearchCoordinator>(registry, options, [clock] {
        return std::chrono::steady_clock::time_point(std::chrono::milliseconds(clock->load()));
    });
    liberomnia::http::configure_application(drogon::app(), config, coordinator);
    using Callback = std::function<void(const drogon::HttpResponsePtr&)>;
    // Test-only control routes release responses and advance a virtual clock.
    drogon::app().registerHandler("/test/complete/{1}/{2}",
        [controls](const drogon::HttpRequestPtr&, Callback&& callback, std::string id, std::size_t index) {
            auto response = drogon::HttpResponse::newHttpResponse();
            const auto found = controls.find(id);
            if (found == controls.end() || found->second->call_count() <= index) response->setStatusCode(drogon::k409Conflict);
            else {
                found->second->wait_call(index).complete({{liberomnia::test::record(id)}, {}, true});
                response->setStatusCode(drogon::k204NoContent);
            }
            callback(response);
        }, {drogon::Post});
    drogon::app().registerHandler("/test/advance/{1}",
        [clock, coordinator](const drogon::HttpRequestPtr&, Callback&& callback, int milliseconds) {
            clock->fetch_add(milliseconds);
            coordinator->sweep();
            auto response = drogon::HttpResponse::newHttpResponse();
            response->setStatusCode(drogon::k204NoContent);
            callback(response);
        }, {drogon::Post});
    drogon::app().run();
}
