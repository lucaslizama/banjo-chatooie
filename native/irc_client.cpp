#include "irc_client.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <random>

#if defined(_WIN32)
#   define WIN32_LEAN_AND_MEAN
#   include <winsock2.h>
#   include <ws2tcpip.h>
    using socket_t = SOCKET;
#   define INVALID_SOCKET_VALUE INVALID_SOCKET
#   define close_socket closesocket
#else
#   include <arpa/inet.h>
#   include <netdb.h>
#   include <netinet/in.h>
#   include <netinet/tcp.h>
#   include <sys/socket.h>
#   include <sys/types.h>
#   include <unistd.h>
    using socket_t = int;
#   define INVALID_SOCKET_VALUE (-1)
#   define close_socket ::close
#endif

namespace twitch {

namespace {

constexpr const char* kHost = "irc.chat.twitch.tv";
// Twitch's plaintext IRC port. TLS lives on 6697, but using it would drag in an
// OpenSSL dependency for a read-only feed of already-public chat, so we stay on
// 6667. Nothing secret is ever sent over this socket -- the login is anonymous.
constexpr const char* kPort = "6667";

constexpr size_t kMaxUserLen = 32;
constexpr size_t kMaxTextLen = 240;

// How long a single recv() may block. Short enough that a shutdown request is
// noticed promptly even if the socket shutdown below were ever to be missed.
constexpr int kRecvTimeoutSeconds = 5;

// Twitch pings roughly every five minutes; if nothing at all arrives for well
// past that, the connection is dead and worth rebuilding.
constexpr int kSilenceLimitSeconds = 360;

void log(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    std::fprintf(stderr, "[twitch-chat] ");
    std::vfprintf(stderr, fmt, args);
    std::fprintf(stderr, "\n");
    va_end(args);
}

// Strips control characters and collapses runs of whitespace. Chat is arbitrary
// user input and goes straight into the UI, so it gets cleaned before it is
// queued rather than trusted.
std::string sanitize(const std::string& in, size_t max_len) {
    std::string out;
    out.reserve(in.size());
    bool prev_space = false;
    for (unsigned char c : in) {
        if (c < 0x20 || c == 0x7F) {
            c = ' ';
        }
        if (c == ' ') {
            if (prev_space || out.empty()) {
                continue;
            }
            prev_space = true;
        } else {
            prev_space = false;
        }
        out += (char)c;
        if (out.size() >= max_len) {
            break;
        }
    }
    while (!out.empty() && out.back() == ' ') {
        out.pop_back();
    }
    return out;
}

int parse_hex_color(const std::string& value) {
    // Twitch sends "#RRGGBB", or an empty string for chatters who never set one.
    if (value.size() != 7 || value[0] != '#') {
        return -1;
    }
    int result = 0;
    for (size_t i = 1; i < 7; i++) {
        char c = value[i];
        int digit;
        if (c >= '0' && c <= '9') {
            digit = c - '0';
        } else if (c >= 'a' && c <= 'f') {
            digit = c - 'a' + 10;
        } else if (c >= 'A' && c <= 'F') {
            digit = c - 'A' + 10;
        } else {
            return -1;
        }
        result = (result << 4) | digit;
    }
    return result;
}

std::string tag_value(const std::string& tags, const std::string& key) {
    // `tags` is the IRCv3 tag blob without its leading '@': "k=v;k2=v2".
    size_t pos = 0;
    while (pos < tags.size()) {
        size_t end = tags.find(';', pos);
        if (end == std::string::npos) {
            end = tags.size();
        }
        size_t eq = tags.find('=', pos);
        if (eq != std::string::npos && eq < end && tags.compare(pos, eq - pos, key) == 0) {
            return tags.substr(eq + 1, end - eq - 1);
        }
        pos = end + 1;
    }
    return {};
}

bool send_all(socket_t fd, const std::string& data) {
    size_t sent = 0;
    while (sent < data.size()) {
#if defined(_WIN32)
        int n = ::send(fd, data.data() + sent, (int)(data.size() - sent), 0);
#else
        ssize_t n = ::send(fd, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
#endif
        if (n <= 0) {
            return false;
        }
        sent += (size_t)n;
    }
    return true;
}

socket_t connect_to_twitch() {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* results = nullptr;
    if (::getaddrinfo(kHost, kPort, &hints, &results) != 0) {
        return INVALID_SOCKET_VALUE;
    }

    socket_t fd = INVALID_SOCKET_VALUE;
    for (addrinfo* it = results; it != nullptr; it = it->ai_next) {
        fd = ::socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (fd == INVALID_SOCKET_VALUE) {
            continue;
        }
        if (::connect(fd, it->ai_addr, (int)it->ai_addrlen) == 0) {
            break;
        }
        close_socket(fd);
        fd = INVALID_SOCKET_VALUE;
    }
    ::freeaddrinfo(results);

    if (fd != INVALID_SOCKET_VALUE) {
        // Kept short so the reader loop comes up for air regularly and notices a
        // shutdown request. Liveness is judged separately, by counting how long
        // Twitch has been silent across timeouts.
#if defined(_WIN32)
        DWORD timeout_ms = kRecvTimeoutSeconds * 1000;
        ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout_ms, sizeof(timeout_ms));
#else
        timeval timeout{};
        timeout.tv_sec = kRecvTimeoutSeconds;
        ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
#endif
        int one = 1;
        ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (const char*)&one, sizeof(one));
    }
    return fd;
}

std::string normalize_channel(const std::string& channel) {
    std::string out;
    for (char c : channel) {
        if (c == '#' || c == ' ' || c == '\r' || c == '\n') {
            continue;
        }
        out += (char)std::tolower((unsigned char)c);
    }
    return out;
}

} // namespace

// Namespace scope rather than a function-local static, deliberately.
//
// A magic static compiles to a guard byte plus a call to __cxa_guard_acquire
// through the PLT, and repeated core dumps caught exactly that call landing on
// the stub's unrelocated file offset, with the load bias never applied. That
// happened even with the library dlopened RTLD_NOW and linked -z now, which
// this code cannot explain. What it can do is not depend on it: an object at
// namespace scope is constructed by the library initialiser at load time, and
// the accessor becomes a plain address load with no call in it at all.
static IrcClient g_client;

IrcClient& client() {
    return g_client;
}

IrcClient::~IrcClient() {
    stop();
}

void IrcClient::start(const std::string& channel) {
    std::string normalized = normalize_channel(channel);
    if (normalized.empty()) {
        stop();
        return;
    }

    {
        std::lock_guard<std::mutex> lock(channel_mutex_);
        if (running_.load() && channel_ == normalized) {
            return;
        }
    }

    stop();

    {
        std::lock_guard<std::mutex> lock(channel_mutex_);
        channel_ = normalized;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.clear();
    }

    running_.store(true);
    state_.store(State::Connecting);
    thread_ = std::thread(&IrcClient::run, this, normalized);
}

void IrcClient::set_active_socket(intptr_t fd) {
    std::lock_guard<std::mutex> lock(socket_mutex_);
    active_socket_ = fd;
}

// Breaks the reader thread out of a blocking recv() without closing the socket
// out from under it -- the thread still owns the descriptor and closes it itself,
// so there is no window where the number could be reused by another thread.
void IrcClient::interrupt_active_socket() {
    std::lock_guard<std::mutex> lock(socket_mutex_);
    if (active_socket_ != -1) {
#if defined(_WIN32)
        ::shutdown((socket_t)active_socket_, SD_BOTH);
#else
        ::shutdown((socket_t)active_socket_, SHUT_RDWR);
#endif
    }
}

void IrcClient::stop() {
    if (!running_.exchange(false)) {
        if (thread_.joinable()) {
            thread_.join();
        }
        return;
    }
    interrupt_active_socket();
    wake_.notify_all();
    if (thread_.joinable()) {
        thread_.join();
    }
    state_.store(State::Idle);
}

bool IrcClient::pop(ChatMessage& out) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.empty()) {
        return false;
    }
    out = std::move(queue_.front());
    queue_.pop_front();
    return true;
}

void IrcClient::push(ChatMessage&& msg) {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.push_back(std::move(msg));
    while (queue_.size() > queue_limit_) {
        queue_.pop_front();
    }
}

void IrcClient::run(std::string channel) {
#if defined(_WIN32)
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        state_.store(State::Error);
        return;
    }
#endif

    std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<int> nick_dist(10000, 99999);
    int backoff_seconds = 1;

    while (running_.load()) {
        state_.store(State::Connecting);

        socket_t fd = connect_to_twitch();
        if (fd == INVALID_SOCKET_VALUE) {
            log("could not reach %s:%s, retrying in %ds", kHost, kPort, backoff_seconds);
            state_.store(State::Error);
        } else {
            // Anonymous read-only login. No password, no token.
            std::string handshake =
                "CAP REQ :twitch.tv/tags\r\n"
                "NICK justinfan" + std::to_string(nick_dist(rng)) + "\r\n"
                "JOIN #" + channel + "\r\n";

            if (!send_all(fd, handshake)) {
                log("handshake failed");
                state_.store(State::Error);
                close_socket(fd);
                fd = INVALID_SOCKET_VALUE;
            }
        }

        if (fd != INVALID_SOCKET_VALUE) {
            state_.store(State::Connected);
            backoff_seconds = 1;
            set_active_socket((intptr_t)fd);

            std::string buffer;
            char chunk[4096];
            int silent_seconds = 0;
            while (running_.load()) {
#if defined(_WIN32)
                int n = ::recv(fd, chunk, (int)sizeof(chunk), 0);
#else
                ssize_t n = ::recv(fd, chunk, sizeof(chunk), 0);
#endif
                if (n == 0) {
                    break;  // clean close by the far end
                }
                if (n < 0) {
                    // A timeout is the expected case and just means chat is
                    // quiet; anything else is a dead socket. Distinguishing them
                    // portably isn't worth it -- give up only once the silence
                    // has run well past Twitch's ping interval.
                    silent_seconds += kRecvTimeoutSeconds;
                    if (silent_seconds >= kSilenceLimitSeconds) {
                        break;
                    }
                    continue;
                }
                silent_seconds = 0;
                buffer.append(chunk, (size_t)n);

                // A single recv can hold several lines, or half of one.
                size_t start = 0;
                size_t newline;
                while ((newline = buffer.find('\n', start)) != std::string::npos) {
                    std::string line = buffer.substr(start, newline - start);
                    if (!line.empty() && line.back() == '\r') {
                        line.pop_back();
                    }
                    handle_line(line, (int)fd, channel);
                    start = newline + 1;
                }
                buffer.erase(0, start);

                // A line this long is not something Twitch sends; drop it rather
                // than let a malformed stream grow the buffer forever.
                if (buffer.size() > 16 * 1024) {
                    buffer.clear();
                }
            }

            set_active_socket(-1);
            close_socket(fd);
            if (running_.load()) {
                log("disconnected from #%s, reconnecting", channel.c_str());
                state_.store(State::Connecting);
            }
        }

        if (!running_.load()) {
            break;
        }

        std::unique_lock<std::mutex> lock(wake_mutex_);
        wake_.wait_for(lock, std::chrono::seconds(backoff_seconds),
                       [this] { return !running_.load(); });
        backoff_seconds = std::min(backoff_seconds * 2, 60);
    }

    state_.store(State::Idle);

#if defined(_WIN32)
    WSACleanup();
#endif
}

void IrcClient::handle_line(const std::string& line, int socket_fd, const std::string& channel) {
    // Keepalive. Twitch drops clients that don't answer.
    if (line.rfind("PING", 0) == 0) {
        send_all((socket_t)socket_fd, "PONG" + line.substr(4) + "\r\n");
        return;
    }

    std::string tags;
    size_t pos = 0;
    if (!line.empty() && line[0] == '@') {
        size_t space = line.find(' ');
        if (space == std::string::npos) {
            return;
        }
        tags = line.substr(1, space - 1);
        pos = space + 1;
    }

    if (pos >= line.size() || line[pos] != ':') {
        return;
    }
    size_t prefix_end = line.find(' ', pos);
    if (prefix_end == std::string::npos) {
        return;
    }
    std::string prefix = line.substr(pos + 1, prefix_end - pos - 1);

    size_t command_end = line.find(' ', prefix_end + 1);
    if (command_end == std::string::npos) {
        return;
    }
    if (line.compare(prefix_end + 1, command_end - prefix_end - 1, "PRIVMSG") != 0) {
        return;
    }

    // " #channel :text"
    size_t text_start = line.find(" :", command_end);
    if (text_start == std::string::npos) {
        return;
    }
    std::string text = line.substr(text_start + 2);

    // "/me" arrives wrapped in CTCP ACTION markers.
    if (text.size() >= 9 && text.compare(0, 8, "\x01" "ACTION ") == 0 && text.back() == '\x01') {
        text = text.substr(8, text.size() - 9);
    }

    // Prefer the display name (correct casing, may be localized); fall back to
    // the login name from the prefix.
    std::string user = tag_value(tags, "display-name");
    if (user.empty()) {
        size_t bang = prefix.find('!');
        user = (bang == std::string::npos) ? prefix : prefix.substr(0, bang);
    }

    ChatMessage msg;
    msg.user = sanitize(user, kMaxUserLen);
    msg.text = sanitize(text, kMaxTextLen);
    msg.color = parse_hex_color(tag_value(tags, "color"));
    msg.highlighted = tag_value(tags, "msg-id") == "highlighted-message";

    // Moderators carry mod=1; the broadcaster does not, and is identified by the
    // broadcaster badge instead.
    {
        std::string badges = tag_value(tags, "badges");
        msg.privileged = tag_value(tags, "mod") == "1" ||
                         badges.find("broadcaster/") != std::string::npos;
    }

    if (msg.user.empty() || msg.text.empty()) {
        return;
    }

    (void)channel;
    push(std::move(msg));
}

} // namespace twitch
