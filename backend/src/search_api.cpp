#include "liberomnia/search_api.hpp"

#include <algorithm>
#include <array>
#include <ctime>
#include <iomanip>
#include <set>
#include <sstream>

namespace liberomnia::http {
namespace {
search::SearchError invalid(std::string code = "invalid_request", int status = 422) {
    return {std::move(code), status == 400 ? "Request body must be valid JSON without duplicate keys."
                                         : "Search parameters are invalid or exceed allowed limits.", status};
}
bool keys(const Json::Value& object, const std::set<std::string>& allowed) {
    if (!object.isObject()) return false;
    for (const auto& key : object.getMemberNames()) if (!allowed.contains(key)) return false;
    return true;
}
constexpr std::array<std::string_view, 6> access_names{"viewable", "pdf", "epub", "restricted", "metadata_only", "unknown"};
constexpr std::array<std::string_view, 4> evidence_names{"confirmed_occurrence", "source_excerpt", "approximate_match", "unverified"};
Json::Value error_json(const search::SearchError& error) {
    Json::Value result;
    result["code"] = error.code;
    result["message"] = error.message;
    return result;
}
Json::Value record_json(const domain::BibliographicRecord& record) {
    Json::Value result;
    result["sourceId"] = record.source_id;
    result["sourceRecordId"] = record.source_record_id;
    result["title"] = record.title;
    result["authors"] = Json::Value(Json::arrayValue);
    for (const auto& author : record.authors) result["authors"].append(author);
    const auto text = [&](const char* key, const std::optional<std::string>& value) {
        result[key] = value ? Json::Value(*value) : Json::Value();
    };
    text("subtitle", record.subtitle); text("publishedDate", record.published_date);
    text("accessCountry", record.access_country); text("previewUrl", record.preview_url); text("infoUrl", record.info_url);
    result["publicDomain"] = record.public_domain ? Json::Value(*record.public_domain) : Json::Value();
    result["pdfAvailable"] = record.pdf_available ? Json::Value(*record.pdf_available) : Json::Value();
    result["epubAvailable"] = record.epub_available ? Json::Value(*record.epub_available) : Json::Value();
    text("edition", record.edition); text("publisher", record.publisher); text("language", record.language);
    text("volume", record.volume); text("originalUrl", record.original_url);
    text("workId", record.work_id); text("editionId", record.edition_id);
    result["publicationYear"] = record.publication_year ? Json::Value(*record.publication_year) : Json::Value();
    result["originalPublicationYear"] = record.original_publication_year ? Json::Value(*record.original_publication_year) : Json::Value();
    result["identifiers"] = Json::Value(Json::arrayValue);
    for (const auto& id : record.identifiers) {
        Json::Value item; item["scheme"] = id.scheme; item["value"] = id.value;
        result["identifiers"].append(item);
    }
    result["access"] = Json::Value(Json::arrayValue);
    for (const auto& access : record.access) {
        Json::Value item;
        item["availability"] = std::string(access_names.at(static_cast<std::size_t>(access.availability)));
        item["url"] = access.url ? Json::Value(*access.url) : Json::Value();
        item["viewability"] = access.viewability ? Json::Value(*access.viewability) : Json::Value();
        item["format"] = access.format ? Json::Value(*access.format) : Json::Value();
        result["access"].append(item);
    }
    result["textualEvidence"] = Json::Value(Json::arrayValue);
    for (const auto& evidence : record.textual_evidence) {
        Json::Value item;
        item["kind"] = std::string(evidence_names.at(static_cast<std::size_t>(evidence.kind)));
        item["text"] = evidence.text ? Json::Value(*evidence.text) : Json::Value();
        item["location"] = evidence.location ? Json::Value(*evidence.location) : Json::Value();
        result["textualEvidence"].append(item);
    }
    return result;
}
Json::Value session_json(const search::SessionSnapshot& session) {
    Json::Value result;
    result["id"] = session.id;
    result["state"] = std::string(search::name(session.state));
    const auto time = std::chrono::system_clock::to_time_t(session.created_at);
    std::tm utc{};
    gmtime_r(&time, &utc);
    std::ostringstream date;
    date << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    result["createdAt"] = date.str();
    result["finished"] = session.finished;
    result["cancellationRequested"] = session.cancellation_requested;
    result["hasErrors"] = session.has_errors;
    result["sources"] = Json::Value(Json::arrayValue);
    for (const auto& source : session.sources) {
        Json::Value item;
        item["sourceId"] = source.source_id;
        item["state"] = std::string(search::name(source.state));
        item["recordsRetrieved"] = Json::UInt64(source.records_retrieved);
        item["recordsAccepted"] = Json::UInt64(source.records_accepted);
        item["recordsRejected"] = Json::UInt64(source.records_rejected);
        item["searchCompleted"] = source.search_completed;
        item["localFiltersApplied"] = source.local_filters_applied;
        item["fallbackUsed"] = source.fallback_used;
        item["coverageIncomplete"] = source.coverage_incomplete;
        item["resultLimitReached"] = source.result_limit_reached;
        item["error"] = source.error ? error_json(*source.error) : Json::Value();
        item["filters"] = Json::Value(Json::arrayValue);
        for (const auto& filter : source.filters) {
            Json::Value application;
            application["id"] = filter.id; application["support"] = std::string(search::name(filter.support));
            item["filters"].append(application);
        }
        result["sources"].append(item);
    }
    return result;
}
bool valid_id(const std::string& id) {
    return id.size() == 32 && std::all_of(id.begin(), id.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}
using Callback = std::function<void(const drogon::HttpResponsePtr&)>;
template <typename Action>
void respond(const drogon::HttpRequestPtr& request, Callback& callback, const std::string& origin, Action action) {
    try {
        const auto& request_origin = request->getHeader("Origin");
        if (!request_origin.empty() && (origin.empty() || request_origin != origin)) {
            callback(search_response(search::SearchError{"origin_not_allowed", "Origin is not allowed.", 403}));
            return;
        }
        callback(action());
    } catch (...) {
        callback(search_response(search::SearchError{"internal_error", "Unable to process search request.", 500}));
    }
}
} // namespace

ParsedSearch parse_search_request(std::string_view body) {
    if (body.size() > 16384) return invalid("request_too_large", 413);
    Json::CharReaderBuilder builder;
    builder["rejectDupKeys"] = true;
    builder["failIfExtra"] = true;
    builder["allowComments"] = false;
    builder["allowTrailingCommas"] = false;
    builder["stackLimit"] = 16;
    Json::Value input;
    std::string parse_error;
    const std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    if (!reader->parse(body.data(), body.data() + body.size(), &input, &parse_error)) return invalid("invalid_json", 400);
    if (!keys(input, {"text", "field", "publication", "language", "sourceIds", "maxResultsPerSource", "availability"}) ||
        !input["text"].isString()) return invalid();
    domain::SearchRequest request;
    request.text = input["text"].asString();
    if (input.isMember("field")) {
        if (!input["field"].isString()) return invalid();
        const auto field = input["field"].asString();
        if (field == "general") request.field = domain::SearchField::general;
        else if (field == "title") request.field = domain::SearchField::title;
        else if (field == "author") request.field = domain::SearchField::author;
        else if (field == "subject") request.field = domain::SearchField::subject;
        else if (field == "passage") request.field = domain::SearchField::passage;
        else return invalid();
    }
    if (input.isMember("publication")) {
        const auto& publication = input["publication"];
        if (!keys(publication, {"from", "to"})) return invalid();
        for (const auto* key : {"from", "to"}) {
            if (!publication[key].isNull()) {
                if ((publication[key].type() != Json::intValue && publication[key].type() != Json::uintValue) ||
                    !publication[key].isInt()) return invalid();
                (std::string_view(key) == "from" ? request.publication.from : request.publication.to) = publication[key].asInt();
            }
        }
    }
    if (input.isMember("language") && !input["language"].isNull()) {
        if (!input["language"].isString()) return invalid();
        request.language = input["language"].asString();
    }
    if (input.isMember("availability") && !input["availability"].isNull()) {
        if (!input["availability"].isString()) return invalid();
        const auto value = input["availability"].asString();
        const auto found = std::find(access_names.begin(), access_names.end(), value);
        if (found == access_names.end()) return invalid();
        request.availability = static_cast<domain::AccessAvailability>(found - access_names.begin());
    }
    if (input.isMember("sourceIds")) {
        if (!input["sourceIds"].isArray() || input["sourceIds"].size() > 16) return invalid();
        for (const auto& source : input["sourceIds"]) {
            if (!source.isString()) return invalid();
            request.source_ids.push_back(source.asString());
        }
    }
    if (input.isMember("maxResultsPerSource")) {
        if ((input["maxResultsPerSource"].type() != Json::intValue && input["maxResultsPerSource"].type() != Json::uintValue) ||
            !input["maxResultsPerSource"].isUInt()) return invalid();
        request.max_results_per_source = input["maxResultsPerSource"].asUInt();
    }
    if (!request.is_valid()) return invalid();
    return request;
}

drogon::HttpResponsePtr search_response(const search::SessionResult& result, drogon::HttpStatusCode success, bool include_records) {
    Json::Value body;
    body["apiVersion"] = "1";
    if (const auto* error = std::get_if<search::SearchError>(&result)) {
        body["error"] = error_json(*error);
        auto response = drogon::HttpResponse::newHttpJsonResponse(body);
        response->setStatusCode(static_cast<drogon::HttpStatusCode>(error->http_status));
        return response;
    }
    const auto& session = std::get<search::SessionSnapshot>(result);
    body["session"] = session_json(session);
    if (include_records) {
        body["results"] = Json::Value(Json::arrayValue);
        for (const auto& record : session.records) {
            Json::Value item;
            item["record"] = record_json(record.record);
            item["original"] = record_json(record.original);
            body["results"].append(item);
        }
    }
    auto response = drogon::HttpResponse::newHttpJsonResponse(body);
    response->setStatusCode(success);
    if (success == drogon::k202Accepted) response->addHeader("Location", "/api/search/" + session.id);
    return response;
}

void register_search_routes(drogon::HttpAppFramework& app,
                            std::shared_ptr<search::SearchCoordinator> coordinator, std::string origin) {
    if (!coordinator) throw std::invalid_argument("Search coordinator is required");
    app.setCustomErrorHandler([](drogon::HttpStatusCode status) {
        return search_response(search::SearchError{"http_error", "HTTP request could not be processed.", static_cast<int>(status)});
    });
    app.registerHandler("/api/search", [coordinator, origin](const drogon::HttpRequestPtr& request, Callback&& callback) {
        respond(request, callback, origin, [&] {
            const auto type = request->getHeader("Content-Type");
            const auto media = type.substr(0, type.find(';'));
            if (media != "application/json") return search_response(search::SearchError{
                "unsupported_media_type", "Use Content-Type: application/json.", 415});
            const auto parsed = parse_search_request(request->body());
            if (const auto* error = std::get_if<search::SearchError>(&parsed)) return search_response(*error);
            return search_response(coordinator->submit(std::get<domain::SearchRequest>(parsed)), drogon::k202Accepted);
        });
    }, {drogon::Post});
    const auto lookup = [coordinator](const std::string& id, bool results, bool cancel) {
        if (!valid_id(id)) return search_response(search::SearchError{"session_not_found", "Search session does not exist.", 404});
        return search_response(cancel ? coordinator->cancel(id) : coordinator->snapshot(id, results), drogon::k200OK, results);
    };
    app.registerHandler("/api/search/{1}", [lookup, origin](const drogon::HttpRequestPtr& request, Callback&& callback, std::string id) {
        respond(request, callback, origin, [&] { return lookup(id, false, false); });
    }, {drogon::Get});
    app.registerHandler("/api/search/{1}/results", [lookup, origin](const drogon::HttpRequestPtr& request, Callback&& callback, std::string id) {
        respond(request, callback, origin, [&] { return lookup(id, true, false); });
    }, {drogon::Get});
    app.registerHandler("/api/search/{1}/cancel", [lookup, origin](const drogon::HttpRequestPtr& request, Callback&& callback, std::string id) {
        respond(request, callback, origin, [&] { return lookup(id, false, true); });
    }, {drogon::Post});
}
} // namespace liberomnia::http
