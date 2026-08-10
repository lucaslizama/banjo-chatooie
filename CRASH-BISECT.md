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

### The crash, finally located: our library's GOT reads as zero

Runs 4, 4b and 4c all crashed. systemd-coredump had been capturing cores the
whole time (`coredumpctl list`), which is worth knowing before hand-rolling
another core-dump workflow.

Run 4c's core (pid 1046475) is unambiguous. Faulting thread is `[Game] MAIN`,
`rip = 0x0`, SEGV_MAPERR:

    #0  0x0000000000000000
    #1  twitch::RedemptionClient::pop(twitch::ChatMessage&)
    #2  twitch_chat_next_message
    #9  recomp::mods::run_hook
    #16 mainThread_entry

The return address lands one instruction after the FIRST call in `pop`:

    <+23>: call *0x444e3(%rip)   # 0x7f117c0b4fa0
    <+29>: test %eax,%eax         <- return address
    <+31>: jne  ...pop.cold       <- the throw path

So it is `std::mutex::lock()` -> `pthread_mutex_lock`, called through a GOT slot,
and the call went to address 0. This is BEFORE the queue is touched at all -- the
`count_ == 0` test is at <+40>. The earlier theory that the crash needed a
non-empty queue is therefore wrong: this instruction runs on every single frame
whether a message is waiting or not.

Reading the slot: `_GLOBAL_OFFSET_TABLE_ + 592` and everything around it is zero,
while `pthread_mutex_lock` is present in the process at 0x7f12f9ab0230.

That zero is real, not a dump artifact -- which had to be checked, because `.got`
contains zeros in the file on disk (relocations are applied at load), the default
`coredump_filter` is 0x33 which excludes file-backed private pages, and gdb
silently falls back to reading the file for pages absent from a core. It is
present: `maintenance info sections` shows `load55 ALLOC LOAD READONLY
HAS_CONTENTS` covering 0x7f117c0b3000-0x7f117c0b5000, which is exactly the RELRO
region holding the GOT (base 0x7f117c055000 + 0x5e000..0x60000).

What this means, and what it does not:

- The slot was populated and later became zero. It is used on every frame from
  the first frame, and the run survived minutes of messages before dying.
- Nothing ordinary can write it. In the live process the GOT sits in an `r--p`
  mapping (RELRO applied), so a stray store would fault at the writer instead of
  quietly zeroing it. That points at the mapping being replaced or re-zeroed
  rather than written through -- an mmap MAP_FIXED landing on it, or similar.
- It is not a bug in this library's C++. No restructuring of `pop` fixes a GOT
  that stops being valid.
- Removing the mutex from the per-frame path would NOT be a real fix, only a
  change of which call dies first. The reader thread calls libc constantly
  through the same GOT.

The -fno-plt / -Bsymbolic work did not help because it moved the same problem
from `.got.plt` to `.got`. The old "PC=0 or a small unrelocated PLT offset"
readings are both consistent with relocation data reading as zero.

Yesterday's last core (1010939) is also `rip = 0x0`, so also a call through a
null pointer, but frame #3 is `recompui::Element::set_text` and frame #1 does not
resolve against the current .so. Same signature, different path -- suggestive of
one mechanism, not proof of it. Do not write that down as settled.

### CAUSE FOUND: the build script was shooting the running game

`build.sh --deploy` used `cp`, which opens the destination O_TRUNC and rewrites
the SAME inode. The game mmaps the native library from that inode. Overwriting it
mid-session rewrites the file under a live mapping: clean pages are dropped and
re-faulted from the new contents, and pages past the temporary EOF fault
outright. A relocation slot read back as the file's own zeros, because that is
what `.got` contains on disk before a loader fills it in.

The timing is conclusive for run 4c:

    deployed .so mtime   13:51:49.111
    SIGSEGV, rip=0       13:51:50
    inode 4135561 both before and after -- same file, rewritten in place

So the crash was self-inflicted by the development loop, not a defect that ships
to players. It explains what never added up: why it was never reproducible, why
it landed at a different call site each time (whichever indirect call the process
reached first afterwards), and why -fno-plt / -Bsymbolic made no difference --
neither has any bearing on the file changing underneath the mapping.

Fixed by deploying through a temp file and `mv`, which swaps in a NEW inode and
leaves a running game's mapping intact. Verified: after the change a deploy
produced inode 4142243 while the live game kept 4135561 mapped and carried on
speaking, where before it would have died within a second. build.sh also warns
that a running game keeps the files it started with, so a mid-session deploy is
not the build under test.

Caveat on the analysis above: the disassembly attributing the faulting call to
`pthread_mutex_lock` was read from the .so as it existed afterwards, which is not
necessarily the bytes that ran. Treat that attribution as unreliable. `rip = 0`
and the zeroed relocation page are solid.

Proven for run 4c. For the earlier crashes it is the leading candidate rather
than established -- but every one of them happened during a session of repeated
rebuild-and-redeploy, so the same mechanism was available each time.

### Next step

Re-run the ladder without deploying mid-session, and see whether the crash
exists at all any more. If it does, the watchdog idea is still the right tool: a
thread in the native library that once a second re-reads our own
`/proc/self/maps` line and a saved copy of an expected libc pointer, printing the
moment either changes. Reading maps stays safe even if the page is gone, and it
would show WHEN the mapping dies rather than only the aftermath.

Also: `feed.py` in the scratchpad is a deterministic stand-in for the redemption
helper -- same wire format, same port, our messages at our rate. Use it instead
of a live channel. Every crash so far happened against whatever chat said at the
time, which is why none of them were reproducible.
