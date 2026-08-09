#!/usr/bin/env python3
"""Bridges Twitch channel point redemptions into the Banjo-Kazooie mod.

Why this is a separate process
------------------------------
Chat arrives over plain IRC, which the mod's native library reads directly.
Redemptions do not: they are only available through the Helix API over HTTPS,
behind an OAuth token. Doing that inside the native library would mean adding
TLS, JSON and a token store to a C++ library that has to ship prebuilt for three
platforms. Python's standard library already has all of it, so this lives here
and hands finished redemptions to the mod over a local socket.

What it does
------------
1. Authorises once, with the OAuth device flow -- you visit a URL and type a
   code. There is no client secret, so nothing secret lives in this repo.
2. Creates (or finds) a channel point reward that asks the viewer for text.
3. Polls for unfulfilled redemptions of that reward, hands them to the mod, and
   marks them fulfilled so they are not delivered twice.

The token is written to the config file with owner-only permissions. It is never
logged and never leaves this machine except back to Twitch.

Usage:
    ./twitch_redemptions.py --client-id <id>     first run, authorises
    ./twitch_redemptions.py                      afterwards
"""

import argparse
import json
import os
import socket
import stat
import sys
import threading
import time
import urllib.error
import urllib.parse
import urllib.request

ID_BASE = "https://id.twitch.tv"
API_BASE = "https://api.twitch.tv/helix"

# channel:read:redemptions to see them; channel:manage:redemptions to create the
# reward and mark redemptions fulfilled. Nothing broader is requested.
SCOPES = "channel:read:redemptions channel:manage:redemptions"

DEFAULT_CONFIG = os.path.expanduser("~/.config/BanjoRecompiled/twitch_redemptions.json")
DEFAULT_PORT = 47474
DEFAULT_REWARD_TITLE = "Say something in Banjo-Kazooie"

# Helix allows 800 points per minute; one poll every two seconds is nowhere near
# it, and redemptions do not need to be more immediate than that.
POLL_SECONDS = 2.0


def log(message):
    print("[redemptions] %s" % message, flush=True)


# --------------------------------------------------------------------------
# Config, including the token. Written 0600 and never printed.
# --------------------------------------------------------------------------

def load_config(path):
    try:
        with open(path) as handle:
            return json.load(handle)
    except FileNotFoundError:
        return {}
    except json.JSONDecodeError:
        log("config at %s is not valid JSON; starting fresh" % path)
        return {}


def save_config(path, config):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    temp = path + ".tmp"
    with open(temp, "w") as handle:
        json.dump(config, handle, indent=2)
    os.chmod(temp, stat.S_IRUSR | stat.S_IWUSR)      # 0600, owner only
    os.replace(temp, path)


# --------------------------------------------------------------------------
# HTTP
# --------------------------------------------------------------------------

class HttpError(Exception):
    def __init__(self, status, body):
        super().__init__("HTTP %s: %s" % (status, body[:200]))
        self.status = status
        self.body = body


def request(method, url, headers=None, data=None, form=None):
    body = None
    headers = dict(headers or {})

    if form is not None:
        body = urllib.parse.urlencode(form).encode()
        headers["Content-Type"] = "application/x-www-form-urlencoded"
    elif data is not None:
        body = json.dumps(data).encode()
        headers["Content-Type"] = "application/json"

    req = urllib.request.Request(url, data=body, headers=headers, method=method)
    try:
        with urllib.request.urlopen(req, timeout=15) as response:
            text = response.read().decode("utf-8", "replace")
            return json.loads(text) if text else {}
    except urllib.error.HTTPError as exc:
        raise HttpError(exc.code, exc.read().decode("utf-8", "replace")) from None


# --------------------------------------------------------------------------
# OAuth device flow. A public client, so there is no secret to store.
# --------------------------------------------------------------------------

def authorise(client_id):
    start = request("POST", ID_BASE + "/oauth2/device",
                    form={"client_id": client_id, "scopes": SCOPES})

    log("")
    log("  Open  %s" % start.get("verification_uri", "https://www.twitch.tv/activate"))
    log("  Code  %s" % start["user_code"])
    log("")
    log("Waiting for you to authorise...")

    interval = max(int(start.get("interval", 5)), 1)
    deadline = time.time() + int(start.get("expires_in", 1800))

    while time.time() < deadline:
        time.sleep(interval)
        try:
            token = request("POST", ID_BASE + "/oauth2/token", form={
                "client_id": client_id,
                "device_code": start["device_code"],
                "grant_type": "urn:ietf:params:oauth:grant-type:device_code",
            })
        except HttpError as exc:
            # authorization_pending is the normal answer until you finish.
            if exc.status in (400, 401) and "authorization_pending" in exc.body:
                continue
            if exc.status == 400 and "slow_down" in exc.body:
                interval += 2
                continue
            raise
        log("Authorised.")
        return token

    raise SystemExit("Authorisation timed out. Run again to retry.")


def refresh(client_id, refresh_token):
    return request("POST", ID_BASE + "/oauth2/token", form={
        "client_id": client_id,
        "grant_type": "refresh_token",
        "refresh_token": refresh_token,
    })


# --------------------------------------------------------------------------
# Helix
# --------------------------------------------------------------------------

class Twitch:
    def __init__(self, config, config_path):
        self.config = config
        self.config_path = config_path

    def _headers(self):
        return {
            "Client-Id": self.config["client_id"],
            "Authorization": "Bearer " + self.config["access_token"],
        }

    def call(self, method, path, params=None, data=None, retry=True):
        url = API_BASE + path
        if params:
            url += "?" + urllib.parse.urlencode(params, doseq=True)
        try:
            return request(method, url, headers=self._headers(), data=data)
        except HttpError as exc:
            if exc.status == 401 and retry and self.config.get("refresh_token"):
                log("token expired, refreshing")
                token = refresh(self.config["client_id"], self.config["refresh_token"])
                self.config["access_token"] = token["access_token"]
                self.config["refresh_token"] = token.get(
                    "refresh_token", self.config["refresh_token"])
                save_config(self.config_path, self.config)
                return self.call(method, path, params, data, retry=False)
            raise

    def broadcaster_id(self):
        if not self.config.get("broadcaster_id"):
            users = self.call("GET", "/users")["data"]
            self.config["broadcaster_id"] = users[0]["id"]
            self.config["broadcaster_login"] = users[0]["login"]
            save_config(self.config_path, self.config)
        return self.config["broadcaster_id"]

    def ensure_reward(self, title, cost):
        """Finds our reward by title, creating it if it isn't there yet.

        Only rewards created by this client id are visible here, which is the
        same restriction that makes polling redemptions possible at all.
        """
        broadcaster = self.broadcaster_id()

        existing = self.call("GET", "/channel_points/custom_rewards", params={
            "broadcaster_id": broadcaster,
            "only_manageable_rewards": "true",
        })["data"]

        for reward in existing:
            if reward["title"] == title:
                return reward["id"]

        created = self.call("POST", "/channel_points/custom_rewards", params={
            "broadcaster_id": broadcaster,
        }, data={
            "title": title,
            "cost": cost,
            "prompt": "Type what a Banjo-Kazooie character should say. "
                      "Start with a name and a colon to pick one, e.g. 'mumbo: hello'.",
            "is_user_input_required": True,
            "should_redemptions_skip_request_queue": False,
        })["data"][0]

        log("created the reward %r for %d points" % (title, cost))
        return created["id"]

    def unfulfilled(self, reward_id):
        return self.call("GET", "/channel_points/custom_rewards/redemptions", params={
            "broadcaster_id": self.broadcaster_id(),
            "reward_id": reward_id,
            "status": "UNFULFILLED",
            "first": 50,
        })["data"]

    def fulfil(self, reward_id, redemption_ids):
        self.call("PATCH", "/channel_points/custom_rewards/redemptions", params={
            "broadcaster_id": self.broadcaster_id(),
            "reward_id": reward_id,
            "id": redemption_ids,
        }, data={"status": "FULFILLED"})


# --------------------------------------------------------------------------
# Local socket. The mod's native library connects here and reads lines.
# --------------------------------------------------------------------------

class Feed:
    """Serves redemptions as tab-separated lines to whoever is connected.

    Bound to loopback only: this is a channel between two processes on this
    machine, not a service.
    """

    def __init__(self, port):
        self.clients = []
        self.lock = threading.Lock()
        self.server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.server.bind(("127.0.0.1", port))
        self.server.listen(4)
        threading.Thread(target=self._accept, daemon=True).start()
        log("listening on 127.0.0.1:%d" % port)

    def _accept(self):
        while True:
            try:
                conn, _ = self.server.accept()
            except OSError:
                return
            with self.lock:
                self.clients.append(conn)
            log("mod connected")

    def send(self, user, text):
        # Tabs and newlines are the framing, so they cannot appear in the data.
        clean = lambda s: s.replace("\t", " ").replace("\r", " ").replace("\n", " ")
        line = ("%s\t%s\n" % (clean(user), clean(text))).encode("utf-8")

        with self.lock:
            alive = []
            for conn in self.clients:
                try:
                    conn.sendall(line)
                    alive.append(conn)
                except OSError:
                    try:
                        conn.close()
                    except OSError:
                        pass
                    log("mod disconnected")
            self.clients = alive


# --------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--client-id", help="Twitch application client id, from "
                                            "https://dev.twitch.tv/console")
    parser.add_argument("--config", default=DEFAULT_CONFIG)
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    parser.add_argument("--reward-title", default=DEFAULT_REWARD_TITLE)
    parser.add_argument("--cost", type=int, default=100,
                        help="channel point cost of the reward when it is created")
    parser.add_argument("--reauthorise", action="store_true",
                        help="discard the stored token and authorise again")
    args = parser.parse_args()

    config = load_config(args.config)

    if args.client_id:
        config["client_id"] = args.client_id
    if not config.get("client_id"):
        raise SystemExit(
            "No client id. Create an application at https://dev.twitch.tv/console\n"
            "(OAuth redirect URL http://localhost, category Application Integration),\n"
            "then run:  %s --client-id <your client id>" % sys.argv[0])

    if args.reauthorise:
        config.pop("access_token", None)
        config.pop("refresh_token", None)

    if not config.get("access_token"):
        token = authorise(config["client_id"])
        config["access_token"] = token["access_token"]
        config["refresh_token"] = token.get("refresh_token")
        save_config(args.config, config)

    twitch = Twitch(config, args.config)
    reward_id = config.get("reward_id")
    if not reward_id or config.get("reward_title") != args.reward_title:
        reward_id = twitch.ensure_reward(args.reward_title, args.cost)
        config["reward_id"] = reward_id
        config["reward_title"] = args.reward_title
        save_config(args.config, config)

    log("watching %r on channel %s" % (args.reward_title,
                                       config.get("broadcaster_login", "?")))

    feed = Feed(args.port)

    while True:
        try:
            pending = twitch.unfulfilled(reward_id)
            if pending:
                # Oldest first, so chat arrives in the order it was redeemed.
                pending.sort(key=lambda r: r["redeemed_at"])
                for redemption in pending:
                    feed.send(redemption["user_name"], redemption.get("user_input", ""))
                twitch.fulfil(reward_id, [r["id"] for r in pending])
        except HttpError as exc:
            log("Twitch said no: %s" % exc)
            time.sleep(5)
        except OSError as exc:
            log("network trouble: %s" % exc)
            time.sleep(5)

        time.sleep(POLL_SECONDS)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        log("stopped")
