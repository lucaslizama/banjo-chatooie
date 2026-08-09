// Read-only Twitch IRC client.
//
// Twitch allows anonymous read access to any public channel's chat: log in as
// `justinfanNNNN` with no password and JOIN the channel. That means this mod
// never asks for, stores, or transmits an OAuth token -- there are no
// credentials anywhere in this repo, and there should never be any.
//
// Runs on its own thread and hands finished messages to the game thread through
// a small locked queue.

#ifndef TWITCH_IRC_CLIENT_H
#define TWITCH_IRC_CLIENT_H

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

namespace twitch {

enum class State : int {
    Idle = 0,
    Connecting = 1,
    Connected = 2,
    Error = 3,
};

struct ChatMessage {
    std::string user;
    std::string text;
    // 0xRRGGBB, or -1 when the chatter has never picked a colour.
    int32_t color = -1;
    // Set for messages sent through the built-in "Highlight My Message" channel
    // point reward, which Twitch marks with msg-id=highlighted-message. This is
    // the one redemption an anonymous reader can see. Channel points need
    // Twitch Affiliate status, so this never fires on a plain channel.
    bool highlighted = false;
    // The broadcaster or one of their moderators. Works on any channel.
    bool privileged = false;
    // Came from a channel point redemption via the helper process rather than
    // from chat. See redemption_client.h.
    bool redeemed = false;
};

class IrcClient {
public:
    ~IrcClient();

    // Connects to `channel` (with or without a leading '#'). Calling this with a
    // different channel reconnects; calling it with the current one is a no-op.
    void start(const std::string& channel);
    void stop();

    State state() const { return state_.load(std::memory_order_relaxed); }

    // Pops the oldest queued message. Returns false when the queue is empty.
    bool pop(ChatMessage& out);

    // Drops queued messages beyond this count, oldest first. Keeps a chat that
    // outruns the game from growing without bound.
    void set_queue_limit(size_t limit) { queue_limit_ = limit; }

private:
    void run(std::string channel);
    void handle_line(const std::string& line, int socket_fd, const std::string& channel);
    void push(ChatMessage&& msg);

    void set_active_socket(intptr_t fd);
    void interrupt_active_socket();

    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<State> state_{State::Idle};

    // The socket the reader thread is currently blocked on, or -1. stop() shuts
    // it down to break that thread out of recv() immediately; without this,
    // joining waits out the whole receive timeout and the game hangs on exit.
    intptr_t active_socket_ = -1;
    std::mutex socket_mutex_;

    std::mutex mutex_;
    std::deque<ChatMessage> queue_;
    size_t queue_limit_ = 64;

    std::string channel_;
    std::mutex channel_mutex_;

    // Lets stop() and reconnects wake the backoff sleep instead of waiting it out.
    std::mutex wake_mutex_;
    std::condition_variable wake_;
};

// Process-wide instance; the mod only ever needs one connection.
IrcClient& client();

} // namespace twitch

#endif // TWITCH_IRC_CLIENT_H
