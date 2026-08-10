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

// Cleans a chat message for display: control characters become spaces, runs of
// whitespace collapse, and the result is truncated to `max_len` bytes WITHOUT
// ever splitting a UTF-8 sequence. That last part matters -- the overlay hands
// this straight to the UI's text renderer, and half a character is not valid
// UTF-8. Invalid input sequences are dropped rather than passed through.
std::string sanitize_text(const std::string& in, size_t max_len);

enum class State : int {
    Idle = 0,
    Connecting = 1,
    Connected = 2,
    Error = 3,
};

// Deliberately plain data: fixed buffers, no std::string, no pointers.
//
// Every crash in this mod has been the native library calling out to something
// and landing on a bad address, and the bisection narrowed it to the moment
// RedemptionClient::pop first had a non-empty queue -- the first time it reached
// the move and the destruction, which free string buffers. Copying a struct of
// arrays instead calls nothing at all. The capacities match the guest buffers in
// twitch_chat_abi.h, so a message that fits here fits there.
struct ChatMessage {
    char user[40];              // TWITCH_USER_CAPACITY
    char text[256];             // TWITCH_TEXT_CAPACITY
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

    bool empty() const { return user[0] == '\0' || text[0] == '\0'; }
};

// Copies `src` into a fixed field, truncating on a UTF-8 boundary so the result
// is always a valid, terminated string.
void set_field(char* dst, size_t capacity, const std::string& src);

// Fixed-capacity ring of messages, and allocation-free on purpose.
//
// pop() runs on the game thread every frame. A std::deque frees a block when one
// empties, which is another call out from the very code that has been failing, so
// this is a plain array with a head and a count. Pushing into a full ring drops
// the oldest, because the newest chat is the interesting chat.
template <size_t Capacity>
class MessageRing {
public:
    void push(const ChatMessage& msg) {
        if (count_ == Capacity) {
            head_ = (head_ + 1) % Capacity;
            count_--;
        }
        slots_[(head_ + count_) % Capacity] = msg;
        count_++;
    }

    bool pop(ChatMessage& out) {
        if (count_ == 0) {
            return false;
        }
        out = slots_[head_];
        head_ = (head_ + 1) % Capacity;
        count_--;
        return true;
    }

    void clear() {
        head_ = 0;
        count_ = 0;
    }

private:
    ChatMessage slots_[Capacity]{};
    size_t head_ = 0;
    size_t count_ = 0;
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

private:
    void run(std::string channel);
    void handle_line(const std::string& line, int socket_fd, const std::string& channel);
    void push(const ChatMessage& msg);

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
    MessageRing<64> queue_;

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
