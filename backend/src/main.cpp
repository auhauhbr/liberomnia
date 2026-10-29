#include "liberomnia/api.hpp"
#include "liberomnia/config.hpp"
#include "liberomnia/google_books_provider.hpp"
#include "liberomnia/source_catalog.hpp"

#include <exception>
#include <filesystem>
#include <iostream>

int main() {
    try {
        const auto config = liberomnia::Config::from_environment();
        std::filesystem::create_directories(config.data_directory / "runtime");
        auto sources = liberomnia::source_catalog();
        std::vector<std::shared_ptr<liberomnia::search::SearchProvider>> adapters;
        auto options = config.google_books;
        options.enabled = config.google_books_enabled;
        auto books = std::make_shared<liberomnia::search::GoogleBooksProvider>(
            liberomnia::search::make_books_http_client(), std::move(options));
        for (auto& source : sources) if (source.id == books->source().id) source = books->source();
        adapters.push_back(std::move(books));
        auto registry = std::make_shared<liberomnia::search::ProviderRegistry>(std::move(sources), std::move(adapters));
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
