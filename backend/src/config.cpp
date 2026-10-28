#include "liberomnia/config.hpp"

#include <arpa/inet.h>
#include <charconv>
#include <cstdlib>
#include <regex>
#include <stdexcept>

namespace liberomnia {
namespace {
unsigned int parse_port(std::string_view value, std::string_view name) {
    unsigned int port{};
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), port);
    if (error != std::errc{} || end != value.data() + value.size() || port == 0 || port > 65535) {
        throw std::invalid_argument(std::string(name) + " must be an integer between 1 and 65535");
    }
    return port;
}

bool is_ip_address(const std::string& value) {
    in6_addr buffer{};
    return inet_pton(AF_INET, value.c_str(), &buffer) == 1 ||
           inet_pton(AF_INET6, value.c_str(), &buffer) == 1;
}

void validate_origin(const std::string& origin) {
    if (origin.empty()) {
        return; // An empty origin disables cross-origin access.
    }
    static const std::regex pattern(R"(^https?://(\[[0-9a-fA-F:]+\]|[A-Za-z0-9](?:[A-Za-z0-9.-]*[A-Za-z0-9])?)(?::([0-9]+))?$)");
    std::smatch match;
    if (!std::regex_match(origin, match, pattern)) {
        throw std::invalid_argument("LIBEROMNIA_FRONTEND_ORIGIN must be an HTTP(S) origin without a path");
    }
    const auto host = match[1].str();
    if (host.front() == '[' && !is_ip_address(host.substr(1, host.size() - 2))) {
        throw std::invalid_argument("LIBEROMNIA_FRONTEND_ORIGIN contains an invalid IPv6 address");
    }
    if (match[2].matched) {
        (void)parse_port(match[2].str(), "Origin port");
    }
}
} // namespace

Config Config::from_environment() {
    return from_environment([](std::string_view name) -> std::optional<std::string> {
        if (const auto* value = std::getenv(std::string(name).c_str())) {
            return std::string(value);
        }
        return std::nullopt;
    });
}

Config Config::from_environment(const Environment& environment) {
    Config config;
    if (auto value = environment("LIBEROMNIA_ADDRESS")) {
        config.address = *value;
    }
    if (!is_ip_address(config.address)) {
        throw std::invalid_argument("LIBEROMNIA_ADDRESS must be a numeric IPv4 or IPv6 address");
    }
    if (auto value = environment("LIBEROMNIA_PORT")) {
        config.port = static_cast<std::uint16_t>(parse_port(*value, "LIBEROMNIA_PORT"));
    }
    if (auto value = environment("LIBEROMNIA_DATA_DIR")) {
        if (value->empty() || value->find('\0') != std::string::npos) {
            throw std::invalid_argument("LIBEROMNIA_DATA_DIR must be a nonempty filesystem path");
        }
        config.data_directory = *value;
    }
    if (auto value = environment("LIBEROMNIA_FRONTEND_ORIGIN")) {
        config.frontend_origin = *value;
    }
    validate_origin(config.frontend_origin);
    return config;
}

std::filesystem::path Config::database_path() const {
    return data_directory / "liberomnia.sqlite3";
}
} // namespace liberomnia
