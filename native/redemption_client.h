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

    // Names the channel point reward the helper should watch, so it can be set
    // from the mod's options screen rather than a command line flag. Sent on the
    // next connect, and immediately if already connected. Safe to call every poll
    // with an unchanged value; only changes go on the wire.
    void set_reward_title(const char* title);

    bool pop(ChatMessage& out);

private:
    void run(int port);
    // Writes the current title upstream. Returns false if the socket is gone.
    bool send_reward_title(intptr_t fd);
    void handle_line(const std::string& line);
    void set_active_socket(intptr_t fd);
    void interrupt_active_socket();

    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<State> state_{State::Idle};

    std::mutex mutex_;
    MessageRing<32> queue_;

    intptr_t active_socket_ = -1;
    std::mutex socket_mutex_;

    // Fixed buffer, not a std::string: set_reward_title runs on the game thread
    // every settings poll, and that path is kept allocation-free.
    char reward_title_[160] = {0};
    std::mutex title_mutex_;

    std::mutex wake_mutex_;
    std::condition_variable wake_;
};

RedemptionClient& redemptions();

} // namespace twitch

#endif // TWITCH_REDEMPTION_CLIENT_H
