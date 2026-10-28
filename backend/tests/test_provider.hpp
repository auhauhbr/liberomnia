#pragma once

#include "liberomnia/search_provider.hpp"

#include <condition_variable>
#include <memory>
#include <mutex>
#include <stdexcept>

namespace liberomnia::test {

// No network, threads, or timers: tests explicitly release each asynchronous call.
class ControlledProvider final : public search::SearchProvider {
public:
    struct Call {
        domain::SearchRequest request;
        std::stop_token stop;
        Completion complete;
    };
    explicit ControlledProvider(std::string id) {
        source_.id = std::move(id);
        source_.name = source_.id;
        source_.implementation_status = ImplementationStatus::available;
        caps.fields = {domain::SearchField::general, domain::SearchField::title};
    }
    const Source& source() const noexcept override { return source_; }
    search::ProviderCapabilities capabilities() const override { return caps; }
    void search(domain::SearchRequest request, std::stop_token stop, Completion complete) override {
        if (throws) throw std::runtime_error("private credential and local infrastructure detail");
        std::lock_guard lock(mutex_);
        calls_.push_back({std::move(request), stop, std::move(complete)});
        changed_.notify_all();
    }
    Call wait_call(std::size_t index = 0) {
        std::unique_lock lock(mutex_);
        if (!changed_.wait_for(lock, std::chrono::seconds(5), [&] { return calls_.size() > index; }))
            throw std::runtime_error("Provider was not dispatched");
        return calls_[index];
    }
    std::size_t call_count() {
        std::lock_guard lock(mutex_);
        return calls_.size();
    }
    search::ProviderCapabilities caps;
    bool throws{false};
private:
    Source source_;
    std::mutex mutex_;
    std::condition_variable changed_;
    std::vector<Call> calls_;
};

inline domain::BibliographicRecord record(std::string source, std::string id = "1", int year = 1899) {
    domain::BibliographicRecord value;
    value.source_id = std::move(source);
    value.source_record_id = std::move(id);
    value.title = "  A   library\nrecord  ";
    value.publication_year = year;
    return value;
}
} // namespace liberomnia::test
