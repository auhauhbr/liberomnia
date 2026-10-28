#include "liberomnia/google_books_provider.hpp"

#include <drogon/drogon.h>
#include <trantor/net/Channel.h>
#include <curl/curl.h>
#include <poll.h>
#include <map>
#include <stdexcept>

namespace liberomnia::search {
namespace {
// Each transfer owns its reactor registration. All curl operations, including
// teardown, run on the application event loop. No worker sleeps or polling loops.
struct Transfer : std::enable_shared_from_this<Transfer> {
    trantor::EventLoop* loop;
    CURLM* multi{};
    CURL* easy{};
    std::map<curl_socket_t, std::shared_ptr<trantor::Channel>> channels;
    trantor::TimerId timer{trantor::InvalidTimerId};
    std::shared_ptr<Transfer> keep_alive;
    std::unique_ptr<std::stop_callback<std::function<void()>>> cancellation;
    BooksHttpClient::Callback callback;
    BooksHttpResponse response;
    std::size_t max_bytes{}, header_bytes{};
    bool oversized{}, done{};

    explicit Transfer(trantor::EventLoop* event_loop) : loop(event_loop) {}
    void remove(curl_socket_t fd) {
        const auto found = channels.find(fd);
        if (found == channels.end()) return;
        auto channel = found->second;
        channel->disableAll(); channel->remove(); channels.erase(found);
        // The poller may still have this Channel in the current event batch.
        loop->queueInLoop([channel] {});
    }
    static int socket_changed(CURL*, curl_socket_t fd, int what, void* data, void*) noexcept {
        auto& self = *static_cast<Transfer*>(data);
        try {
            if (what == CURL_POLL_REMOVE) { self.remove(fd); return 0; }
            auto& channel = self.channels[fd];
            if (!channel) {
                channel = std::make_shared<trantor::Channel>(self.loop, fd);
                channel->tie(self.shared_from_this());
                std::weak_ptr<Transfer> weak = self.shared_from_this();
                channel->setEventCallback([weak, fd] {
                    auto transfer = weak.lock();
                    if (!transfer || transfer->done) return;
                    auto found = transfer->channels.find(fd);
                    if (found == transfer->channels.end()) return;
                    const auto events = found->second->revents();
                    int flags = 0;
                    if (events & (POLLIN | POLLHUP)) flags |= CURL_CSELECT_IN;
                    if (events & POLLOUT) flags |= CURL_CSELECT_OUT;
                    if (events & (POLLERR | POLLNVAL)) flags |= CURL_CSELECT_ERR;
                    transfer->action(fd, flags);
                });
            }
            int events = 0;
            if (what == CURL_POLL_IN || what == CURL_POLL_INOUT) events |= trantor::Channel::kReadEvent;
            if (what == CURL_POLL_OUT || what == CURL_POLL_INOUT) events |= trantor::Channel::kWriteEvent;
            channel->updateEvents(events);
            return 0;
        } catch (...) { return -1; }
    }
    static int timer_changed(CURLM*, long ms, void* data) noexcept {
        auto& self = *static_cast<Transfer*>(data);
        try {
            if (self.timer != trantor::InvalidTimerId) self.loop->invalidateTimer(self.timer);
            self.timer = trantor::InvalidTimerId;
            if (ms >= 0 && !self.done) {
                std::weak_ptr<Transfer> weak = self.shared_from_this();
                self.timer = self.loop->runAfter(std::max(0.001, ms / 1000.0), [weak] {
                    if (auto transfer = weak.lock(); transfer && !transfer->done) {
                        transfer->timer = trantor::InvalidTimerId;
                        transfer->action(CURL_SOCKET_TIMEOUT, 0);
                    }
                });
            }
            return 0;
        } catch (...) { return -1; }
    }
    static std::size_t body(char* data, std::size_t size, std::size_t n, void* context) noexcept {
        auto& self = *static_cast<Transfer*>(context);
        const auto bytes = size * n;
        if (bytes > self.max_bytes - self.response.body.size()) { self.oversized = true; return 0; }
        try { self.response.body.append(data, bytes); return bytes; } catch (...) { return 0; }
    }
    static std::size_t header(char* data, std::size_t size, std::size_t n, void* context) noexcept {
        auto& self = *static_cast<Transfer*>(context);
        const auto bytes = size * n;
        if (bytes > 32768 - self.header_bytes) { self.oversized = true; return 0; }
        self.header_bytes += bytes;
        try {
            std::string line(data, bytes);
            auto colon = line.find(':');
            auto name = line.substr(0, colon);
            for (auto& c : name) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
            if (name == "retry-after") {
                auto value = line.substr(colon + 1);
                auto begin = value.find_first_not_of(" \t");
                auto end = value.find_last_not_of(" \t\r\n");
                if (begin != std::string::npos && end != std::string::npos) self.response.retry_after = value.substr(begin, end-begin+1);
            }
            return bytes;
        } catch (...) { return 0; }
    }
    void finish(std::optional<ProviderError> error = {}) {
        if (done) return;
        done = true;
        auto lifetime = shared_from_this();
        response.error = error;
        if (timer != trantor::InvalidTimerId) loop->invalidateTimer(timer);
        if (multi && easy) curl_multi_remove_handle(multi, easy);
        if (easy) { curl_easy_cleanup(easy); easy = nullptr; }
        if (multi) { curl_multi_cleanup(multi); multi = nullptr; }
        while (!channels.empty()) remove(channels.begin()->first);
        cancellation.reset();
        keep_alive.reset();
        auto complete = std::move(callback);
        complete(std::move(response));
    }
    void action(curl_socket_t fd, int flags) {
        if (done) return;
        int running{};
        if (curl_multi_socket_action(multi, fd, flags, &running) != CURLM_OK) return finish(ProviderError::network);
        int queued{};
        while (auto* message = curl_multi_info_read(multi, &queued)) {
            if (message->msg != CURLMSG_DONE) continue;
            long status{}; curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &status);
            response.status = static_cast<int>(status);
            if (oversized) return finish(ProviderError::invalid_response);
            if (message->data.result == CURLE_OPERATION_TIMEDOUT) return finish(ProviderError::timed_out);
            if (message->data.result != CURLE_OK) return finish(ProviderError::network);
            return finish();
        }
    }
    void start(std::string query, const std::string& api_key, std::chrono::milliseconds timeout,
               std::size_t limit, std::stop_token stop) {
        keep_alive = shared_from_this(); max_bytes = limit;
        if (stop.stop_requested()) return finish(ProviderError::cancelled);
        easy = curl_easy_init(); multi = curl_multi_init();
        if (!easy || !multi) return finish(ProviderError::network);
        if (!api_key.empty()) {
            char* escaped = curl_easy_escape(easy, api_key.data(), static_cast<int>(api_key.size()));
            if (!escaped) return finish(ProviderError::network);
            query += "&key="; query += escaped; curl_free(escaped);
        }
        const auto url = "https://www.googleapis.com/books/v1/volumes?" + query;
        curl_easy_setopt(easy, CURLOPT_URL, url.c_str());
        curl_easy_setopt(easy, CURLOPT_PROTOCOLS_STR, "https");
        curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 0L);
        curl_easy_setopt(easy, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(easy, CURLOPT_SSL_VERIFYHOST, 2L);
        curl_easy_setopt(easy, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(easy, CURLOPT_TIMEOUT_MS, static_cast<long>(timeout.count()));
        curl_easy_setopt(easy, CURLOPT_CONNECTTIMEOUT_MS, static_cast<long>(timeout.count()));
        curl_easy_setopt(easy, CURLOPT_ACCEPT_ENCODING, "identity");
        curl_easy_setopt(easy, CURLOPT_USERAGENT, "LiberOmnia/0.1");
        curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, &Transfer::body);
        curl_easy_setopt(easy, CURLOPT_WRITEDATA, this);
        curl_easy_setopt(easy, CURLOPT_HEADERFUNCTION, &Transfer::header);
        curl_easy_setopt(easy, CURLOPT_HEADERDATA, this);
        curl_multi_setopt(multi, CURLMOPT_SOCKETFUNCTION, &Transfer::socket_changed);
        curl_multi_setopt(multi, CURLMOPT_SOCKETDATA, this);
        curl_multi_setopt(multi, CURLMOPT_TIMERFUNCTION, &Transfer::timer_changed);
        curl_multi_setopt(multi, CURLMOPT_TIMERDATA, this);
        std::weak_ptr<Transfer> weak = shared_from_this();
        cancellation = std::make_unique<std::stop_callback<std::function<void()>>>(stop, [weak] {
            if (auto self = weak.lock()) self->loop->queueInLoop([self] { self->finish(ProviderError::cancelled); });
        });
        if (curl_multi_add_handle(multi, easy) != CURLM_OK) finish(ProviderError::network);
    }
};
class CurlBooksClient final : public BooksHttpClient {
public:
    CurlBooksClient() {
        static const auto initialized = curl_global_init(CURL_GLOBAL_DEFAULT);
        if (initialized != CURLE_OK || !(curl_version_info(CURLVERSION_NOW)->features & CURL_VERSION_ASYNCHDNS) ||
            !(curl_version_info(CURLVERSION_NOW)->features & CURL_VERSION_SSL))
            throw std::runtime_error("Google Books requires libcurl with asynchronous DNS and TLS");
    }
    void get(std::string query, std::string api_key, std::chrono::milliseconds timeout,
             std::size_t max_bytes, std::stop_token stop, Callback callback) override {
        auto* loop = drogon::app().getLoop();
        auto transfer = std::make_shared<Transfer>(loop);
        transfer->callback = std::move(callback);
        loop->queueInLoop([transfer, query = std::move(query), api_key = std::move(api_key), timeout, max_bytes, stop] {
            try { transfer->start(query, api_key, timeout, max_bytes, stop); }
            catch (...) { transfer->finish(ProviderError::network); }
        });
    }
};
}
std::shared_ptr<BooksHttpClient> make_books_http_client() { return std::make_shared<CurlBooksClient>(); }
} // namespace liberomnia::search
