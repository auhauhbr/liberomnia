#include "liberomnia/google_books_provider.hpp"
#include "liberomnia/config.hpp"
#include <drogon/drogon.h>
#include <iostream>
#include <map>

// Optional live diagnostic. Retains only this search's response in memory and
// compares normalized records with the actual upstream fields before exiting.
class InspectingClient final : public liberomnia::search::BooksHttpClient {
public:
    std::shared_ptr<BooksHttpClient> real = liberomnia::search::make_books_http_client();
    std::map<std::string, Json::Value> volumes;
    void get(std::string query, std::string key, std::chrono::milliseconds timeout, std::size_t bytes,
             std::stop_token stop, Callback callback) override {
        real->get(std::move(query), std::move(key), timeout, bytes, stop, [this, callback](liberomnia::search::BooksHttpResponse response) {
            std::cout << "Upstream HTTP " << response.status << '\n';
            Json::CharReaderBuilder builder; Json::Value body; std::string error;
            std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
            if (reader->parse(response.body.data(), response.body.data()+response.body.size(), &body, &error) && body.isObject()) {
                if (body["items"].isArray()) for (const auto& item : body["items"])
                    if (item.isObject() && item["id"].isString()) volumes[item["id"].asString()] = item;
                if (body["error"].isObject() && body["error"]["status"].isString())
                    std::cout << "Upstream status: " << body["error"]["status"].asString() << '\n';
            }
            callback(std::move(response));
        });
    }
};
int main() {
    auto client = std::make_shared<InspectingClient>();
    auto options = liberomnia::Config::from_environment().google_books;
    options.max_pages = 1; options.call_budget = 1;
    auto provider = std::make_shared<liberomnia::search::GoogleBooksProvider>(client, options);
    liberomnia::domain::SearchRequest request;
    request.text = "The Imitation of Christ"; request.max_results_per_source = 3;
    int exit_code = 0;
    provider->search(request, {}, [&](liberomnia::search::ProviderResult result) {
        if (result.error) {
            std::cout << "Live search unavailable; typed provider error " << static_cast<int>(*result.error) << ". No books asserted.\n";
            exit_code = 2;
        } else {
            std::cout << "Received " << result.records.size() << " records. Coverage incomplete.\n";
            for (const auto& record : result.records) {
                const auto& raw = client->volumes.at(record.source_record_id)["volumeInfo"];
                if (record.title != raw["title"].asString() || (record.published_date && *record.published_date != raw["publishedDate"].asString())) exit_code = 1;
                std::cout << record.source_record_id << " | " << record.title << " | " << record.published_date.value_or("date absent") << '\n';
            }
        }
        drogon::app().quit();
    });
    drogon::app().setLogLevel(trantor::Logger::kWarn).run();
    return exit_code;
}
