#include "liberomnia/api.hpp"
#include "liberomnia/config.hpp"

#include <exception>
#include <filesystem>
#include <iostream>

int main() {
    try {
        const auto config = liberomnia::Config::from_environment();
        std::filesystem::create_directories(config.data_directory / "runtime");
        liberomnia::http::configure_application(drogon::app(), config);
        std::cout << "LiberOmnia listening on " << config.address << ':' << config.port << '\n';
        drogon::app().run();
    } catch (const std::exception& error) {
        std::cerr << "LiberOmnia startup failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
