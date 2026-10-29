#include "liberomnia/google_books_provider.hpp"
#include "liberomnia/normalization.hpp"

#include <curl/curl.h>
#include <algorithm>
#include <atomic>
#include <charconv>
#include <deque>
#include <mutex>
#include <set>
#include <regex>
#include <sstream>
#include <stdexcept>

namespace liberomnia::search {
namespace {
using namespace domain;
const Json::Value& member(const Json::Value& v, const char* key) {
    static const Json::Value empty;
    return v.isObject() ? v[key] : empty;
}
std::optional<std::string> text(const Json::Value& v, const char* key, std::size_t max = 8192) {
    const auto& value = member(v, key);
    if (!value.isString()) return {};
    auto s = value.asString();
    if (s.empty() || s.size() > max || normalize_spaces(s).empty()) return {};
    return s;
}
std::optional<bool> boolean(const Json::Value& v, const char* key) {
    const auto& value = member(v, key);
    return value.isBool() ? std::optional(value.asBool()) : std::nullopt;
}
std::optional<std::string> url(const Json::Value& v, const char* key) {
    auto s = text(v, key, 2048);
    if (!s || s->find_first_of("\\\r\n\t <>") != std::string::npos || s->find('@') != std::string::npos ||
        std::any_of(s->begin(), s->end(), [](unsigned char c) { return c < 32 || c == 127; })) return {};
    const auto start = s->starts_with("https://") ? 8U : s->starts_with("http://") ? 7U : 0U;
    if (!start || s->size() <= start || (*s)[start] == '/' || (*s)[start] == '?' || (*s)[start] == '#') return {};
    return s;
}
std::vector<std::string> strings(const Json::Value& value, std::size_t max = 64) {
    std::vector<std::string> out;
    if (value.isArray()) for (const auto& item : value) {
        if (out.size() == max) break;
        if (item.isString() && item.asString().size() <= 8192 && !normalize_spaces(item.asString()).empty())
            out.push_back(item.asString());
    }
    return out;
}
std::string encode(std::string_view value) {
    constexpr char hex[] = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : value) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') out += static_cast<char>(c);
        else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
    }
    return out;
}
std::string folded(std::string value) {
    for (char& c : value) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return normalize_spaces(value);
}
// Conservative token equality: never claim an exact phrase/edition match.
// Non-ASCII bytes remain unchanged; accented case variants may be missed.
bool field_matches(const Json::Value& item, const SearchRequest& request) {
    const auto& info = member(item, "volumeInfo");
    std::vector<std::string> fields;
    if (request.field == SearchField::title) {
        if (auto title = text(info, "title")) fields.push_back(*title);
    } else if (request.field == SearchField::author) fields = strings(member(info, "authors"));
    else fields = strings(member(info, "categories"));
    const auto tokenize = [](std::string value) {
        for (char& c : value) if (static_cast<unsigned char>(c) < 128 && !std::isalnum(static_cast<unsigned char>(c))) c = ' ';
        std::istringstream stream(folded(std::move(value)));
        std::set<std::string> tokens;
        for (std::string token; stream >> token;) tokens.insert(token);
        return tokens;
    };
    const auto wanted = tokenize(request.text);
    if (wanted.empty()) return false;
    return std::any_of(fields.begin(), fields.end(), [&](const auto& field) {
        const auto have = tokenize(field);
        return std::includes(have.begin(), have.end(), wanted.begin(), wanted.end());
    });
}
bool language_valid(const std::optional<std::string>& language) {
    static const std::set<std::string> supported{"pt", "en", "fr", "la", "es", "it", "de"};
    return !language || supported.contains(*language);
}
}

struct BooksRateLimiter::State {
    std::mutex mutex;
    std::size_t active{}, concurrency, per_minute;
    Clock now;
    WallClock wall_now;
    std::deque<std::chrono::steady_clock::time_point> calls;
    std::chrono::steady_clock::time_point blocked_until{};
};
BooksRateLimiter::BooksRateLimiter(std::size_t concurrency, std::size_t per_minute, Clock now, WallClock wall_now) : state_(std::make_shared<State>()) {
    if (!concurrency || !per_minute || !now || !wall_now) throw std::invalid_argument("Invalid rate limits");
    state_->concurrency = concurrency; state_->per_minute = per_minute; state_->now = std::move(now); state_->wall_now = std::move(wall_now);
}
bool BooksRateLimiter::acquire() {
    std::lock_guard lock(state_->mutex);
    const auto now = state_->now();
    while (!state_->calls.empty() && state_->calls.front() + std::chrono::minutes(1) <= now) state_->calls.pop_front();
    if (now < state_->blocked_until || state_->active >= state_->concurrency || state_->calls.size() >= state_->per_minute) return false;
    ++state_->active; state_->calls.push_back(now); return true;
}
void BooksRateLimiter::release(std::string_view retry_after) {
    { std::lock_guard lock(state_->mutex); if (state_->active) --state_->active; }
    defer(retry_after);
}
bool BooksRateLimiter::ready() const {
    std::lock_guard lock(state_->mutex);
    const auto now = state_->now();
    return now >= state_->blocked_until && state_->active < state_->concurrency &&
        static_cast<std::size_t>(std::count_if(state_->calls.begin(), state_->calls.end(), [&](auto call) {
            return call + std::chrono::minutes(1) > now;
        })) < state_->per_minute;
}
void BooksRateLimiter::defer(std::string_view retry_after) {
    std::lock_guard lock(state_->mutex);
    if (retry_after.empty()) return;
    long seconds{};
    const auto [end, error] = std::from_chars(retry_after.data(), retry_after.data() + retry_after.size(), seconds);
    if (error != std::errc{} || end != retry_after.data() + retry_after.size()) {
        const auto date = curl_getdate(std::string(retry_after).c_str(), nullptr);
        seconds = date < 0 ? 60 : std::max<long>(0, date - state_->wall_now());
    }
    // Saturate only to prevent time_point overflow; never retry before Retry-After.
    if (seconds < 0) seconds = 60;
    const auto now = state_->now();
    const auto capacity = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::time_point::max() - now).count();
    seconds = std::min<long>(seconds, capacity);
    state_->blocked_until = std::max(state_->blocked_until, now + std::chrono::seconds(seconds));
}

std::optional<int> GoogleBooksProvider::publication_year(std::string_view date) {
    if (date.size() != 4 && date.size() != 7 && date.size() != 10) return {};
    for (std::size_t i = 0; i < date.size(); ++i) {
        if (i == 4 || i == 7) { if (date[i] != '-') return {}; }
        else if (date[i] < '0' || date[i] > '9') return {};
    }
    const auto number = [&](std::size_t i, std::size_t n) { int v{}; std::from_chars(date.data()+i, date.data()+i+n, v); return v; };
    int year = number(0, 4);
    if (!year) return {};
    if (date.size() > 4) {
        int month = number(5, 2);
        if (month < 1 || month > 12) return {};
        if (date.size() == 10) {
            constexpr int days[]{31,28,31,30,31,30,31,31,30,31,30,31};
            int max = days[month-1] + (month == 2 && year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
            if (number(8,2) < 1 || number(8,2) > max) return {};
        }
    }
    return year;
}
std::optional<BibliographicRecord> GoogleBooksProvider::record(const Json::Value& item) {
    const auto& info = member(item, "volumeInfo");
    const auto id = text(item, "id", 256), title = text(info, "title");
    if (!id || !title) return {};
    BibliographicRecord r;
    r.source_id = "google-books"; r.source_record_id = *id; r.title = *title;
    r.subtitle = text(info, "subtitle"); r.authors = strings(member(info, "authors"));
    r.publisher = text(info, "publisher"); r.language = text(info, "language", 35);
    r.published_date = text(info, "publishedDate", 64);
    if (r.published_date) r.publication_year = publication_year(*r.published_date);
    r.original_url = url(info, "canonicalVolumeLink"); r.info_url = url(info, "infoLink"); r.preview_url = url(info, "previewLink");
    if (!r.original_url) r.original_url = r.info_url;
    const auto& ids = member(info, "industryIdentifiers");
    if (ids.isArray()) for (const auto& entry : ids) {
        if (r.identifiers.size() == 32) break;
        auto type = text(entry, "type", 128), value = text(entry, "identifier", 256);
        if (type && value) r.identifiers.push_back({*type, *value});
    }
    const auto& access = member(item, "accessInfo");
    r.public_domain = boolean(access, "publicDomain"); r.access_country = text(access, "country", 8);
    auto view = text(access, "viewability", 32);
    auto reader = url(access, "webReaderLink");
    if (!reader) reader = r.preview_url;
    if (view == "ALL_PAGES" || view == "PARTIAL") r.access.push_back({AccessAvailability::viewable, reader, view, {}});
    else if (view == "NO_PAGES") r.access.push_back({AccessAvailability::metadata_only, {}, view, {}});
    else r.access.push_back({AccessAvailability::unknown, {}, {}, {}});
    for (const auto* format : {"pdf", "epub"}) {
        const auto& data = member(access, format);
        const auto available = boolean(data, "isAvailable");
        (std::string_view(format) == "pdf" ? r.pdf_available : r.epub_available) = available;
        const auto download = url(data, "downloadLink");
        const auto token = text(data, "acsTokenLink", 2048);
        if (available == true && download && !token) r.access.push_back({std::string_view(format) == "pdf" ?
            AccessAvailability::pdf : AccessAvailability::epub, download, {}, format});
        else if (token) r.access.push_back({AccessAvailability::restricted, {}, {}, format});
    }
    if (auto snippet = text(member(item, "searchInfo"), "textSnippet"))
        r.textual_evidence.push_back({EvidenceKind::source_excerpt, snippet, {}});
    auto normalized = normalize_record(r, "google-books");
    return normalized ? std::optional(std::move(r)) : std::nullopt;
}
std::string GoogleBooksProvider::query(const SearchRequest& request, bool fallback) {
    std::string terms = normalize_spaces(request.text), prefix;
    if (!fallback) switch (request.field) {
        case SearchField::title: prefix = "intitle:"; break;
        case SearchField::author: prefix = "inauthor:"; break;
        case SearchField::subject: prefix = "subject:"; break;
        default: break;
    }
    if (!prefix.empty() || request.field == SearchField::passage) {
        std::erase(terms, '"');
        terms = '"' + terms + '"';
    }
    std::string result = "q=" + encode(prefix + terms) + "&printType=books";
    if (request.language) result += "&langRestrict=" + encode(*request.language);
    return result;
}

namespace {
bool administrative(ProviderError error) {
    switch (error) {
        case ProviderError::configuration_missing: case ProviderError::configuration_invalid:
        case ProviderError::key_invalid: case ProviderError::api_disabled: case ProviderError::key_restricted:
        case ProviderError::quota_daily_exhausted: case ProviderError::quota_daily_zero:
        case ProviderError::authentication: case ProviderError::blocked: return true;
        default: return false;
    }
}
std::string operational_name(ProviderError error) {
    switch (error) {
        case ProviderError::configuration_missing: return "configuration_required";
        case ProviderError::configuration_invalid: case ProviderError::key_invalid:
        case ProviderError::api_disabled: case ProviderError::key_restricted:
        case ProviderError::authentication: return "configuration_error";
        case ProviderError::quota_daily_exhausted: case ProviderError::quota_daily_zero: return "quota_exhausted";
        case ProviderError::rate_limit_minute: case ProviderError::rate_limited: return "rate_limited";
        case ProviderError::blocked: return "permission_denied";
        case ProviderError::network: case ProviderError::timed_out: return "communication_error";
        default: return "temporary_failure";
    }
}
std::optional<ProviderError> classify_error(int status, const Json::Value& root) {
    const auto& error = member(root, "error");
    if (status == 200 && error.isNull()) return {};
    const auto& code = member(error, "code");
    if (status == 200 && code.isInt() && code.asInt() >= 400 && code.asInt() <= 599) status = code.asInt();
    std::set<std::string> reasons;
    std::string quota_evidence;
    bool daily_zero = false;
    const auto daily_period = [](const std::string& value) {
        static const std::regex day_unit(R"(/d(?:/|\s|$))");
        return value.find("perday") != std::string::npos || value.find("per day") != std::string::npos ||
            value.find("daily") != std::string::npos || std::regex_search(value, day_unit);
    };
    const auto zero_value = [](const Json::Value& value) {
        return (value.isString() && value.asString() == "0") || (value.isUInt64() && value.asUInt64() == 0);
    };
    const auto collect = [&](const Json::Value& details) {
        if (!details.isArray()) return;
        for (const auto& detail : details) {
            if (auto reason = text(detail, "reason", 256)) reasons.insert(*reason);
            const auto& metadata = member(detail, "metadata");
            // Only inspect known quota fields; project IDs and raw metadata never escape.
            std::string period;
            for (const auto* key : {"quota_limit", "quota_metric", "quota_unit"})
                if (auto value = text(metadata, key, 512)) period += " " + folded(*value);
            quota_evidence += period;
            daily_zero = daily_zero || (daily_period(period) && zero_value(member(metadata, "quota_limit_value")));
            const auto& violations = member(detail, "violations");
            if (violations.isArray()) for (const auto& violation : violations) {
                std::string description;
                for (const auto* key : {"quotaId", "quotaMetric", "description"})
                    if (auto value = text(violation, key, 512)) description += " " + folded(*value);
                quota_evidence += description;
                daily_zero = daily_zero || (daily_period(description) && zero_value(member(violation, "quotaValue")));
            }
        }
    };
    collect(member(error, "errors")); collect(member(error, "details"));
    const auto has = [&](std::initializer_list<const char*> values) {
        return std::any_of(values.begin(), values.end(), [&](const char* value) { return reasons.contains(value); });
    };
    if (has({"API_KEY_INVALID", "keyInvalid"})) return ProviderError::key_invalid;
    if (has({"SERVICE_DISABLED", "API_DISABLED", "accessNotConfigured"})) return ProviderError::api_disabled;
    if (has({"API_KEY_SERVICE_BLOCKED", "API_KEY_HTTP_REFERRER_BLOCKED", "API_KEY_IP_ADDRESS_BLOCKED",
             "API_KEY_ANDROID_APP_BLOCKED", "API_KEY_IOS_APP_BLOCKED"})) return ProviderError::key_restricted;
    if (has({"CREDENTIALS_MISSING"})) return ProviderError::configuration_missing;
    if (has({"authError", "API_KEY_EXPIRED"}) || status == 401 || text(error, "status") == "UNAUTHENTICATED")
        return ProviderError::authentication;
    const bool quota = status == 429 || text(error, "status") == "RESOURCE_EXHAUSTED" ||
        has({"RATE_LIMIT_EXCEEDED", "QUOTA_EXCEEDED", "rateLimitExceeded", "userRateLimitExceeded", "dailyLimitExceeded", "dailyLimitExceededUnreg"});
    if (quota) {
        // Human-readable messages are supplementary evidence only, never public diagnostics.
        const auto message = folded(text(error, "message").value_or(""));
        quota_evidence += " " + message;
        const bool daily = has({"dailyLimitExceeded", "dailyLimitExceededUnreg"}) || daily_period(quota_evidence);
        static const std::regex zero_limit(R"((?:quota|limit|value)\s*(?:value|is|of)?\s*[:=]?\s*['"]?0(?:[^0-9]|$))");
        daily_zero = daily_zero || (daily_period(message) && std::regex_search(message, zero_limit));
        if (daily) return daily_zero ? ProviderError::quota_daily_zero : ProviderError::quota_daily_exhausted;
        if (quota_evidence.find("perminute") != std::string::npos || quota_evidence.find("per minute") != std::string::npos ||
            quota_evidence.find("/min/") != std::string::npos) return ProviderError::rate_limit_minute;
        return ProviderError::rate_limited;
    }
    if (status == 403 || text(error, "status") == "PERMISSION_DENIED") return ProviderError::blocked;
    if (status == 400) return ProviderError::configuration_invalid;
    if (status >= 500) return ProviderError::failed;
    return ProviderError::invalid_response;
}
}

// Shared lifetime with callbacks, independent of the provider object's lifetime.
// Administrative failures latch for this configuration. Other in-flight successes
// cannot erase them. Recreate the provider after an administrator corrects settings.
struct BooksOperationalState {
    mutable std::mutex mutex;
    std::optional<ProviderError> last_error;
    std::optional<ProviderError> blocked;
    std::optional<std::int64_t> last_success;
    void observe(std::optional<ProviderError> error, bool success, BooksRateLimiter& limiter) {
        std::lock_guard lock(mutex);
        if (error == ProviderError::cancelled || error == ProviderError::invalid_request) return;
        if (success) last_success = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        if (blocked) return;
        last_error = error;
        if (error && administrative(*error)) blocked = error;
        else if (error && *error != ProviderError::rate_limited && *error != ProviderError::rate_limit_minute)
            limiter.defer("5");
    }
    std::optional<ProviderError> rejection() const {
        std::lock_guard lock(mutex);
        return blocked;
    }
    std::optional<ProviderError> admit(BooksRateLimiter& limiter) const {
        std::lock_guard lock(mutex);
        if (blocked) return blocked;
        // Serialize admission with administrative failure publication.
        if (!limiter.acquire()) return ProviderError::rate_limited;
        return {};
    }
};

namespace {
struct Operation : std::enable_shared_from_this<Operation> {
    std::recursive_mutex mutex;
    std::shared_ptr<BooksHttpClient> client;
    std::shared_ptr<BooksRateLimiter> limiter;
    GoogleBooksOptions options;
    std::shared_ptr<BooksOperationalState> state;
    bool successful_response{};
    SearchRequest request;
    std::stop_token stop;
    SearchProvider::Completion completion;
    std::unique_ptr<std::stop_callback<std::function<void()>>> stop_callback;
    ProviderResult result;
    std::chrono::steady_clock::time_point deadline;
    std::size_t calls{}, pages{}, offset{};
    bool done{};
    void finish(std::optional<ProviderError> error = {}) {
        std::lock_guard lock(mutex);
        if (done) return;
        done = true; result.error = error;
        if (calls) state->observe(error, successful_response, *limiter);
        auto callback = std::move(completion);
        callback(std::move(result));
    }
    void next() {
        std::lock_guard lock(mutex);
        if (done) return;
        if (stop.stop_requested()) return finish(ProviderError::cancelled);
        auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
        if (remaining.count() <= 0) return finish(ProviderError::timed_out);
        if (calls >= options.call_budget || pages >= options.max_pages || result.records.size() >= request.max_results_per_source) {
            result.result_limit_reached = true; return finish();
        }
        if (auto rejected = state->admit(*limiter)) return finish(rejected);
        ++calls; ++pages;
        const auto size = std::min(options.page_size, request.max_results_per_source - result.records.size());
        auto query = GoogleBooksProvider::query(request, result.fallback_used) + "&startIndex=" + std::to_string(offset) + "&maxResults=" + std::to_string(size);
        auto once = std::make_shared<std::atomic<bool>>(false);
        auto receive = [self = shared_from_this(), once, size](BooksHttpResponse response) {
            if (once->exchange(true)) return;
            self->limiter->release();
            try { self->receive(std::move(response), size); }
            catch (...) { self->finish(ProviderError::invalid_response); }
        };
        try { client->get(std::move(query), options.api_key, remaining, options.max_response_bytes, stop, receive); }
        catch (...) { receive({0, {}, {}, ProviderError::network}); }
    }
    void receive(BooksHttpResponse response, std::size_t size) {
        std::lock_guard lock(mutex);
        if (done) return;
        if (stop.stop_requested()) return finish(ProviderError::cancelled);
        if (std::chrono::steady_clock::now() >= deadline) return finish(ProviderError::timed_out);
        if (response.error) return finish(response.error);
        if (response.body.size() > options.max_response_bytes) return finish(ProviderError::invalid_response);
        Json::CharReaderBuilder builder;
        builder["stackLimit"] = 32; builder["rejectDupKeys"] = true; builder["failIfExtra"] = true;
        std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
        Json::Value root; std::string errors;
        const bool parsed = reader->parse(response.body.data(), response.body.data()+response.body.size(), &root, &errors) && root.isObject();
        if (auto error = classify_error(response.status, root)) {
            if (*error == ProviderError::rate_limited || *error == ProviderError::rate_limit_minute)
                limiter->defer(response.retry_after.empty() ? "60" : response.retry_after);
            else if (!administrative(*error) && !response.retry_after.empty()) limiter->defer(response.retry_after);
            return finish(error);
        }
        if (response.status != 200 || !parsed || root.isMember("error") || !root["totalItems"].isUInt64() ||
            (root.isMember("items") && !root["items"].isArray())) return finish(ProviderError::invalid_response);
        const auto& items = member(root, "items");
        if (items.size() > size || (items.empty() && root["totalItems"].asUInt64() != 0 && !root.isMember("items")))
            return finish(ProviderError::invalid_response);
        if (items.empty()) successful_response = true;
        if (items.empty() && offset == 0 && !result.fallback_used && root["totalItems"].asUInt64() == 0 &&
            (request.field == SearchField::title || request.field == SearchField::author || request.field == SearchField::subject) &&
            calls < options.call_budget) {
            result.fallback_used = true;
            // Exactly one alternative general page, even if the native page budget is one.
            pages = 0; return next();
        }
        bool invalid = false;
        for (const auto& item : items) {
            auto r = GoogleBooksProvider::record(item);
            if (!r) { invalid = true; continue; }
            if (result.fallback_used && !field_matches(item, request)) continue;
            if (std::none_of(result.records.begin(), result.records.end(), [&](const auto& old) { return old.source_record_id == r->source_record_id; }))
                result.records.push_back(std::move(*r));
        }
        offset += items.size();
        // Google totalItems is approximate. Never claim exhaustive collection coverage.
        if (invalid) return finish(ProviderError::invalid_response);
        successful_response = true;
        if (result.fallback_used || items.size() < size) {
            result.result_limit_reached = result.fallback_used && items.size() == size;
            return finish();
        }
        next();
    }
};
}
GoogleBooksProvider::GoogleBooksProvider(std::shared_ptr<BooksHttpClient> client, GoogleBooksOptions options,
                                       std::shared_ptr<BooksRateLimiter> limiter)
    : client_(std::move(client)), options_(std::move(options)), limiter_(std::move(limiter)), state_(std::make_shared<BooksOperationalState>()) {
    if (!client_ || !options_.is_valid()) throw std::invalid_argument("Invalid Google Books configuration");
    if (!limiter_) { static auto shared = std::make_shared<BooksRateLimiter>(); limiter_ = shared; }
    source_ = {"google-books", "Google Books", IntegrationType::api, ImplementationStatus::available,
        {{"general", CapabilityStatus::confirmed}, {"title", CapabilityStatus::confirmed}, {"author", CapabilityStatus::confirmed},
         {"subject", CapabilityStatus::confirmed}, {"passage", CapabilityStatus::confirmed}, {"language", CapabilityStatus::confirmed},
         {"publicationRange", CapabilityStatus::confirmed}, {"availability", CapabilityStatus::confirmed}},
        "Bounded live metadata search. Field searches may use one locally verified fallback. Edition dates and availability are filtered locally; coverage is incomplete. Snippets are source excerpts, not confirmed occurrences. Access varies by country; upstream quotas may prevent searches."};
}
ImplementationStatus GoogleBooksProvider::implementation_status() const noexcept {
    return options_.enabled && !options_.api_key.empty() && !state_->rejection() && limiter_->ready()
        ? ImplementationStatus::available : ImplementationStatus::temporarily_unavailable;
}
std::optional<ProviderOperationalStatus> GoogleBooksProvider::operational_status() const {
    std::lock_guard lock(state_->mutex);
    ProviderOperationalStatus status;
    status.configured = !options_.api_key.empty();
    status.last_successful_at = state_->last_success;
    status.error = state_->blocked ? state_->blocked : state_->last_error;
    status.search_allowed = options_.enabled && status.configured && !state_->blocked && limiter_->ready();
    if (!options_.enabled) status.state = "disabled";
    else if (!status.configured) { status.state = "configuration_required"; status.error = ProviderError::configuration_missing; }
    else if (status.error) status.state = operational_name(*status.error);
    else if (!limiter_->ready()) status.state = "rate_limited";
    else status.state = state_->last_success ? "available" : "unverified";
    return status;
}
const Source& GoogleBooksProvider::source() const noexcept { return source_; }
ProviderCapabilities GoogleBooksProvider::capabilities() const {
    return {{SearchField::general, SearchField::title, SearchField::author, SearchField::subject, SearchField::passage},
        FilterSupport::local, FilterSupport::native, FilterSupport::local};
}
void GoogleBooksProvider::search(SearchRequest request, std::stop_token stop, Completion completion) {
    if (!request.is_valid() || !language_valid(request.language)) {
        completion({{}, ProviderError::invalid_request, false}); return;
    }
    if (!options_.enabled || options_.api_key.empty()) {
        completion({{}, ProviderError::configuration_missing, false}); return;
    }
    if (const auto blocked = state_->rejection()) { completion({{}, blocked, false}); return; }
    auto operation = std::make_shared<Operation>();
    operation->client = client_; operation->limiter = limiter_; operation->options = options_;
    operation->request = std::move(request); operation->stop = stop;
    operation->state = state_;
    operation->completion = std::move(completion);
    operation->deadline = std::chrono::steady_clock::now() + options_.timeout;
    operation->stop_callback = std::make_unique<std::stop_callback<std::function<void()>>>(stop,
        [weak = std::weak_ptr(operation)] { if (auto self = weak.lock()) self->finish(ProviderError::cancelled); });
    operation->next();
}
} // namespace liberomnia::search
