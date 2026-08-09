// Reads channel point redemptions from the local helper process.
//
// Redemptions are not carried over IRC -- they only exist behind the Helix API,
// over HTTPS with an OAuth token. Rather than put TLS, JSON and a credential
// store into a library that ships prebuilt for three platforms, that work lives
// in helper/twitch_redemptions.py, which listens on loopback and writes one
// redemption per line as "user<TAB>text".
//
// This end just connects and reads, which is the same shape as the IRC reader,
// so it reuses that structure. If the helper is not running, this quietly
// retries and the rest of the mod carries on with chat as normal.

#ifndef TWITCH_REDEMPTION_CLIENT_H
#define TWITCH_REDEMPTION_CLIENT_H

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

#include "irc_client.h"     // for twitch::ChatMessage and twitch::State

namespace twitch {

class RedemptionClient {
public:
    ~RedemptionClient();

    // Starts (or restarts) reading from the helper on the given loopback port.
    void start(int port);
    void stop();

    State state() const { return state_.load(std::memory_order_relaxed); }

    bool pop(ChatMessage& out);

private:
    void run(int port);
    void handle_line(const std::string& line);
    void set_active_socket(intptr_t fd);
    void interrupt_active_socket();

    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<State> state_{State::Idle};

    std::mutex mutex_;
    std::deque<ChatMessage> queue_;
    size_t queue_limit_ = 32;

    intptr_t active_socket_ = -1;
    std::mutex socket_mutex_;

    std::mutex wake_mutex_;
    std::condition_variable wake_;
};

RedemptionClient& redemptions();

} // namespace twitch

#endif // TWITCH_REDEMPTION_CLIENT_H
