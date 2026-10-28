#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace liberomnia {

using Environment = std::function<std::optional<std::string>(std::string_view)>;

struct Config {
    std::string address{"127.0.0.1"};
    std::uint16_t port{8080};
    std::filesystem::path data_directory{"./data"};
    std::string frontend_origin{"http://localhost:5173"};

    [[nodiscard]] static Config from_environment();
    [[nodiscard]] static Config from_environment(const Environment& environment);
    [[nodiscard]] std::filesystem::path database_path() const;
};

} // namespace liberomnia
