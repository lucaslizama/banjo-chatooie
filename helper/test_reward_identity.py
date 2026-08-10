"""Does a second run reuse the reward it created before?

Run it with: python3 helper/test_reward_identity.py

The bug: the saved title was used as a lookup key. Once the mod owns the name,
the saved title is whatever the mod last asked for while --reward-title falls back
to its default, so they disagree on every launch -- and ensure_reward, searching by
the default title, would not find the renamed reward and would create another.

No network: Twitch is stubbed, and ensure_reward raises if it is ever reached.
"""
import importlib.util, json, os, sys, tempfile, threading, time

HELPER = os.path.join(os.path.dirname(os.path.abspath(__file__)), "twitch_redemptions.py")
spec = importlib.util.spec_from_file_location("helper", HELPER)
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)          # safe: main() is behind an __main__ guard

created = []

class StubTwitch:
    def __init__(self, config, path): pass
    def broadcaster_id(self): return "1"
    def ensure_reward(self, title, cost):
        created.append(title)
        return "brand-new-id"
    def rename_reward(self, rid, title): renamed.append((rid, title))
    def unfulfilled(self, rid): raise SystemExit(0)   # stop the poll loop at once
    def fulfil(self, rid, ids): pass

renamed = []
mod.Twitch = StubTwitch
# The poll loop sleeps instead of polling when no mod is connected, so pretend one
# is; unfulfilled() then raises SystemExit and the run ends at a known point.
mod.Feed.has_clients = lambda self: True
mod.POLL_SECONDS = 0

def run(config_dict, argv):
    created.clear(); renamed.clear()
    path = tempfile.mktemp(suffix=".json")
    with open(path, "w") as f:
        json.dump(config_dict, f)
    sys.argv = ["helper", "--config", path, "--token", "t",
                "--broadcaster-id", "1", "--api-base", "http://127.0.0.1:1/mock"] + argv
    try:
        mod.main()
    except SystemExit:
        pass
    with open(path) as f:
        return json.load(f)

base = {"client_id": "c", "access_token": "t", "broadcaster_id": "1"}

# 1. First ever run: no reward_id, so it must create one.
run(dict(base), ["--port", "47591"])
assert created == [mod.DEFAULT_REWARD_TITLE], created
print(f"PASS  first run creates: {created[0]!r}")

# 2. Second run after the mod renamed it. Saved title differs from the flag's
#    default. It must NOT create anything.
after_rename = dict(base, reward_id="existing-id", reward_title="Chat Speaks")
run(after_rename, ["--port", "47592"])
assert created == [], f"created a duplicate reward: {created}"
print("PASS  second run after a rename creates nothing")

# 3. Same again with the default title saved, for completeness.
run(dict(base, reward_id="existing-id", reward_title=mod.DEFAULT_REWARD_TITLE),
    ["--port", "47593"])
assert created == [], created
print("PASS  second run with the original title creates nothing")

# 4. An explicit --reward-title asks for a rename, not a new reward.
run(dict(base, reward_id="existing-id", reward_title="Chat Speaks"),
    ["--port", "47594", "--reward-title", "Something Else"])
assert created == [], created
assert renamed == [("existing-id", "Something Else")], renamed
print(f"PASS  explicit --reward-title renames: {renamed[0][1]!r}")
