# Crash bisection

Symptom: SIGSEGV in the [Game] MAIN thread. The faulting PC is always an
address our library called out to, and it is wrong in a different way each
time (0x33a6, 0x32a6, 0x0). The frame below is always ours: twitch::client(),
RedemptionClient::pop, RedemptionClient::stop.

Ruled out by changing the link, all of which still crashed:
  static libstdc++ / dynamic libstdc++ / no PLT at all
  -z now, -z relro, -z nodelete, -Bsymbolic
  magic statics replaced with namespace-scope objects

Not the cause:
  monitors (tail -f on a log cannot affect the process; real cores captured)
  map transitions with a box open (fixed separately, and genuinely fixed)

| run | configuration                         | result        |
| --- | ------------------------------------- | ------------- |
| 0   | no Banjo-Chatooie at all              | 14m42s clean  |
| 1   | loaded, channel blank, speak Off      | in progress   |
| 2   | + channel set, speak Off              |               |
| 3   | + speak Every message                 |               |
| 4   | + channel points                      |               |

For comparison, crashes with the mod fully active came at roughly 8-10 minutes.

Run 1 distinguishes the per-frame native calls (twitch_chat_get_state and
twitch_chat_next_message, called every frame regardless of configuration) from
the threads, which only exist once a channel or the redemption reader is
running.

Run 1 result: 17m00s clean, Mumbo's Mountain completed 100%.
The per-frame native calls are therefore NOT the cause: the game called
twitch_chat_get_state and twitch_chat_next_message every frame throughout,
and those are where three of the six crashes landed.

What run 1 lacked was threads. Suspicion moves there.

## Threading review, done while run 2 was in flight (no code changed)

Checked for races between the reader thread and the game thread:

- queue_ is only touched under mutex_ in both push and pop. OK.
- running_ and state_ are atomics. OK.
- active_socket_ is set to -1 under socket_mutex_ BEFORE the fd is closed, so
  interrupt_active_socket cannot shutdown() a descriptor that has already been
  closed and possibly reused. OK.
- thread_ is always joined before being reassigned, so no terminate(). OK.

One real defect found, not a crash:

  stop() calls wake_.notify_all() without holding wake_mutex_. If the reader
  thread evaluates its predicate and is descending into wait_for() at that
  moment, the notify is missed and it sleeps out the whole backoff, which grows
  to 60s for IRC and 30s for redemptions. join() then blocks the GAME thread for
  that long. The socket shutdown and the 5s receive timeout usually cover it, but
  the backoff sleep is not covered by either.

  Fix when the bisect is finished: take wake_mutex_ around the flag change, or
  notify while holding it.

Nothing here explains a wild jump. If run 2 crashes, the surface is small enough
to instrument directly rather than reason about.

Run 2 result: 13m38s clean, IRC thread active on a busy channel including a
full disconnect/reconnect cycle. Exonerates the reader thread, the shared
queue, socket teardown and the overlay.

Remaining difference for run 3: dialogue injection.

## Found during run 3, NOT fixed yet (would invalidate the bisect)

Brentilda cannot be talked to while a chat box is on screen. The yield hook
closes our box but relies on the game replaying its own request, which
showDialogConditional only does when the caller passes 0x04 or 0x20 in arg1.
The molehill tested earlier passes one; Brentilda evidently does not, so her
request is dropped and the conversation never starts.

Fix: restore the deferred re-issue removed in 4af2076 -- capture the game's
arguments in the hook and re-issue them once the dialogue system is free. That
version hung during Bottles, but that hang is now understood (DIALOG_STATE_6's
unbounded scan over a blob with one terminator) and fixed by the terminator
padding, so it should be safe for a known reason rather than by hope.

Also found during run 3: Blubber waits for the chat box instead of interrupting
it, while other NPCs interrupt. Cause: SPEAK_CARRIER_ASSET is 0xA0B, which is
Blubber's own first-meeting line, and the yield hook identifies "our own call"
by comparing text_id against it. So Blubber's genuine request looks like ours
and the hook declines to yield. He is the one NPC in the game that cannot
interrupt chat, because we borrowed his line.

Fix: identify our own call with an explicit flag set around the
gcdialog_showDialog call, not by text id. Then the carrier choice stops leaking
into behaviour.

Observed so far, useful for the re-issue work:
  molehill / Bottles  - interrupts correctly (self-queues)
  Blubber             - waits (carrier-asset collision, above)
  Brentilda           - cannot talk at all (does not self-queue, request lost)
  Leaky               - interrupts correctly (self-queues)
  Blubber, gold       - interrupts correctly (different asset id)
  Sandcastle crab     - interrupts correctly

Carrier-asset diagnosis confirmed cleanly: throwing gold to Blubber DOES cancel
the chat box, while his first-meeting line does not. Same NPC, different asset
id, different behaviour -- so it is the id, not the character. Only 0xA0B is
affected, which is exactly our carrier.

## Run 4 result: CRASHED. Bisection complete.

| run | configuration                    | result       |
| --- | -------------------------------- | ------------ |
| 0   | no mod                           | 14m42s clean |
| 1   | loaded, idle                     | 17m00s clean |
| 2   | + chat reader                    | 13m38s clean |
| 3   | + dialogue injection             | 15m16s clean |
| 4   | + channel point redemptions      | CRASHED      |

Backtrace unchanged: RedemptionClient::pop, PC 0.

The narrowing that matters: twitch_chat_next_message calls redemptions().pop()
every frame in EVERY run. In runs 1-3 that queue was always empty, so pop
returned at `if (queue_.empty())`. Run 4 is the first time it went further, into

    out = std::move(queue_.front());
    queue_.pop_front();

which frees string buffers and deque blocks, i.e. calls out to free(). Calling
out is what has failed in every crash tonight.

Note the standalone test (scratchpad/redtest.cpp) drives exactly this path
outside the game and passes, so the code is not wrong in isolation; something
about doing it inside the recomp process is.

Next idea, untried: remove allocation from the queue entirely. Replace the
std::string members of ChatMessage with fixed char arrays, so pop performs no
allocation, no deallocation and no library calls at all on the game thread. That
is consistent with runs 1-3 surviving precisely because they never reached the
deallocating path.

## Session 2 (10 Aug)

Fixed and committed:
- de5b1a8 Blubber (our call marked by a flag, not by asset id), Brentilda
  (deferred re-issue restored), notify_all now holds wake_mutex_.
- 58397d8 Queues are allocation-free: ChatMessage holds fixed char arrays and a
  plain array ring replaces std::deque. pop() is now 40 instructions and three
  calls: pthread_mutex_lock, memcpy, pthread_mutex_unlock. Those two mutex calls
  are what runs 1-3 exercised every frame without trouble.
- fbbe501 speak_clear_chat had a 3328-byte array in a stack frame. The game's
  main thread stack is 0x17F0 = 6128 bytes TOTAL. It fired on every channel
  change. Temporary is static now; largest MIPS frame in the mod is 88 bytes.
  Also write_string took a std::string, so the new char arrays built a temporary
  per message -- an allocation on the game thread. Takes const char* now.

### The "speaking regression" was phantom -- CLOSED

There was no regression. Characters not speaking was the test channel being
silent, not the code failing.

The channel under test was `caseoh_`, which is enormous but was offline, so its
chat was nearly dead. Trigger = Every message on a channel saying nothing looks
exactly like a broken mod. Two things kept the mistake alive: the overlay had
been switched off, so there was no visual confirmation of whether anything was
arriving at all, and a harness driving IrcClient directly popped zero messages in
20 seconds, which read as a broken queue when it was really `state=2` (Connected)
with nothing to deliver.

Settled by a diagnostic build (SPEAK_DEBUG in src/speak.c) that names the guard
each speak_tick declines on, plus a line per arriving message. Run 5 traced a
complete cycle on the first real message the channel produced:

    [twitch-chat] got avaaaaaaaaaishere: "Hi" flags 0x0 trigger 4 -> speak
    [speak] queued (1 waiting) portrait 15: "avaaaaaaaaaishere: Hi"
    [speak] clear to speak (queued 1, ours showing 0, idle 90)
    [speak] showDialog returned 1 for portrait 15
    [speak] a dialogue is already up (queued 0, ours showing 1, idle 0)
    [speak] queue empty (queued 0, ours showing 0, idle 90)

Bottles appeared on screen and spoke it, confirmed visually. No guard blocked
anything, showDialog returned 1. de5b1a8, 58397d8 and fbbe501 are all cleared.

Lesson worth keeping: test against a channel whose input you control. A silent
channel is indistinguishable from a broken pipeline, and it cost most of a
session. Keep the overlay ON while testing for the same reason -- it is the
cheapest possible check that messages are arriving.

Leave SPEAK_DEBUG on while hunting the crash; set it to 0 before releasing.

Crash status: unchanged and still undiagnosed. Runs 4, 4b and 4c all crashed.
They are still not a fair test of the allocation-free queues, though for a
different reason than recorded before: not because speaking was broken, but
because a silent channel means the message path was barely exercised at all.
Re-run the ladder against a channel that is actually talking.
