#pragma once
#include <chrono>
#include <string>

namespace liberomnia::search {
struct GoogleBooksOptions {
    std::string api_key;
    std::size_t page_size{40};
    std::size_t max_pages{3};
    std::size_t call_budget{4};
    std::size_t max_response_bytes{1048576};
    std::chrono::milliseconds timeout{6000};
    [[nodiscard]] bool is_valid() const;
};

}
