#include "redemption_client.h"

#include <chrono>
#include <cstdio>

#if defined(_WIN32)
#   define WIN32_LEAN_AND_MEAN
#   include <winsock2.h>
#   include <ws2tcpip.h>
    using socket_t = SOCKET;
#   define INVALID_SOCKET_VALUE INVALID_SOCKET
#   define close_socket closesocket
#else
#   include <arpa/inet.h>
#   include <netinet/in.h>
#   include <netinet/tcp.h>
#   include <sys/socket.h>
#   include <unistd.h>
    using socket_t = int;
#   define INVALID_SOCKET_VALUE (-1)
#   define close_socket ::close
#endif

namespace twitch {

namespace {

// Matches the caps the IRC reader applies, so a redemption and a chat message
// are interchangeable by the time the game sees them.
constexpr size_t kMaxUserLen = 32;
constexpr size_t kMaxTextLen = 240;

// Short, so a stop request is noticed promptly.
constexpr int kRecvTimeoutSeconds = 2;

socket_t connect_loopback(int port) {
    socket_t fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd == INVALID_SOCKET_VALUE) {
        return INVALID_SOCKET_VALUE;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons((unsigned short)port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (::connect(fd, (sockaddr*)&address, sizeof(address)) != 0) {
        close_socket(fd);
        return INVALID_SOCKET_VALUE;
    }

#if defined(_WIN32)
    DWORD timeout_ms = kRecvTimeoutSeconds * 1000;
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout_ms, sizeof(timeout_ms));
#else
    timeval timeout{};
    timeout.tv_sec = kRecvTimeoutSeconds;
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
#endif
    return fd;
}

} // namespace

// Namespace scope for the same reason as twitch::client(); see irc_client.cpp.
static RedemptionClient g_redemptions;

RedemptionClient& redemptions() {
    return g_redemptions;
}

RedemptionClient::~RedemptionClient() {
    stop();
}

void RedemptionClient::start(int port) {
    if (running_.load()) {
        return;
    }
    stop();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.clear();
    }
    running_.store(true);
    state_.store(State::Connecting);
    thread_ = std::thread(&RedemptionClient::run, this, port);
}

void RedemptionClient::set_active_socket(intptr_t fd) {
    std::lock_guard<std::mutex> lock(socket_mutex_);
    active_socket_ = fd;
}

void RedemptionClient::interrupt_active_socket() {
    std::lock_guard<std::mutex> lock(socket_mutex_);
    if (active_socket_ != -1) {
#if defined(_WIN32)
        ::shutdown((socket_t)active_socket_, SD_BOTH);
#else
        ::shutdown((socket_t)active_socket_, SHUT_RDWR);
#endif
    }
}

void RedemptionClient::stop() {
    if (!running_.exchange(false)) {
        if (thread_.joinable()) {
            thread_.join();
        }
        return;
    }
    interrupt_active_socket();
    // Notify while holding wake_mutex_. Without it, a reader thread that has just
    // evaluated its predicate and is descending into wait_for() misses the
    // wakeup and sleeps out its whole backoff, which grows to 30-60 seconds --
    // and join() below blocks the GAME thread for that long.
    {
        std::lock_guard<std::mutex> lock(wake_mutex_);
        wake_.notify_all();
    }
    if (thread_.joinable()) {
        thread_.join();
    }
    state_.store(State::Idle);
}

bool RedemptionClient::pop(ChatMessage& out) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.empty()) {
        return false;
    }
    out = std::move(queue_.front());
    queue_.pop_front();
    return true;
}

void RedemptionClient::handle_line(const std::string& line) {
    size_t tab = line.find('\t');
    if (tab == std::string::npos) {
        return;
    }

    ChatMessage msg;
    msg.user = sanitize_text(line.substr(0, tab), kMaxUserLen);
    msg.text = sanitize_text(line.substr(tab + 1), kMaxTextLen);
    msg.color = -1;
    msg.highlighted = false;
    msg.redeemed = true;

    if (msg.user.empty() || msg.text.empty()) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    queue_.push_back(std::move(msg));
    while (queue_.size() > queue_limit_) {
        queue_.pop_front();
    }
}

void RedemptionClient::run(int port) {
    // The helper is usually started by hand, so not finding it is normal rather
    // than an error. Back off quietly and keep trying.
    int backoff_seconds = 2;

    while (running_.load()) {
        socket_t fd = connect_loopback(port);

        if (fd == INVALID_SOCKET_VALUE) {
            state_.store(State::Idle);
        } else {
            state_.store(State::Connected);
            backoff_seconds = 2;
            set_active_socket((intptr_t)fd);
            std::fprintf(stderr, "[twitch-chat] redemption helper connected on port %d\n", port);

            std::string buffer;
            char chunk[2048];
            while (running_.load()) {
#if defined(_WIN32)
                int n = ::recv(fd, chunk, (int)sizeof(chunk), 0);
#else
                ssize_t n = ::recv(fd, chunk, sizeof(chunk), 0);
#endif
                if (n == 0) {
                    break;                  // helper exited
                }
                if (n < 0) {
                    continue;               // read timeout; nothing redeemed yet
                }
                buffer.append(chunk, (size_t)n);

                size_t start = 0;
                size_t newline;
                while ((newline = buffer.find('\n', start)) != std::string::npos) {
                    handle_line(buffer.substr(start, newline - start));
                    start = newline + 1;
                }
                buffer.erase(0, start);

                if (buffer.size() > 8 * 1024) {
                    buffer.clear();
                }
            }

            set_active_socket(-1);
            close_socket(fd);
            state_.store(State::Idle);
        }

        if (!running_.load()) {
            break;
        }

        std::unique_lock<std::mutex> lock(wake_mutex_);
        wake_.wait_for(lock, std::chrono::seconds(backoff_seconds),
                       [this] { return !running_.load(); });
        if (backoff_seconds < 30) {
            backoff_seconds *= 2;
        }
    }

    state_.store(State::Idle);
}

} // namespace twitch
