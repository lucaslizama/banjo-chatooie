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


## Queued for 0.2.1

- Brentilda's dialogue is dropped when a chat box is up. Restore the deferred
  re-issue removed in 4af2076; the hang it once caused is understood and fixed.
- Blubber's first-meeting line does not yield, because it IS the carrier asset
  and the hook identifies our own calls by text id. Use an explicit flag instead.
- stop() calls notify_all() without holding wake_mutex_, so a missed wakeup can
  leave the game thread waiting out a backoff of up to 60 seconds.
- Run 4: channel point redemptions, the only configuration the bisect has not
  cleared, and the one every crash so far occurred under.
- The Thunderstore description. 0.2.0 shipped with "BETA, not yet
  stream-tested", which is inaccurate and the wrong warning. The replacement is
  already in release.sh and travels with the next upload, since the description
  lives in the package manifest and cannot be edited after a version is
  published.
- Read the docs over properly. Several places still describe how things worked
  earlier in the day rather than how they work now, and the rename left phrasing
  in odd shapes. Worth a full pass rather than more spot fixes.
