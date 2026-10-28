#include "liberomnia/api.hpp"
#include "liberomnia/config.hpp"
#include "liberomnia/source_catalog.hpp"

#include <exception>
#include <filesystem>
#include <iostream>

int main() {
    try {
        const auto config = liberomnia::Config::from_environment();
        std::filesystem::create_directories(config.data_directory / "runtime");
        auto registry = std::make_shared<liberomnia::search::ProviderRegistry>(liberomnia::source_catalog());
        auto coordinator = std::make_shared<liberomnia::search::SearchCoordinator>(registry, config.search);
        liberomnia::http::configure_application(drogon::app(), config, std::move(coordinator));
        std::cout << "LiberOmnia listening on " << config.address << ':' << config.port << '\n';
        drogon::app().run();
    } catch (const std::exception& error) {
        std::cerr << "LiberOmnia startup failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
