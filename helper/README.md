# Channel point redemption helper

Chat arrives over IRC, which the mod reads by itself. **Channel point redemptions
do not** — they exist only behind Twitch's Helix API, over HTTPS, with an OAuth
token. This script fetches them and hands them to the mod over a local socket.

It only needs Python 3 and its standard library. No pip install.

## Before you start

Channel points require **Twitch Affiliate**. If your channel doesn't have them,
this cannot work, no matter how it's configured.

You also need a Twitch application, which takes a minute to create:

1. Go to https://dev.twitch.tv/console/apps and register an application.
2. Name it anything. Set the OAuth redirect URL to `http://localhost`, and the
   category to *Application Integration*.
3. Copy the **Client ID**. There is no client secret involved — this uses the
   device flow, which is designed for clients that can't keep one.

## First run

```
./helper/twitch_redemptions.py --client-id <your client id>
```

It prints a URL and a code. Open the URL, type the code, approve. That's the only
interactive step — the token is saved and refreshed automatically afterwards.

On first run it also creates a channel point reward called **"Say something in
Banjo-Kazooie"** for 100 points, with viewer text input enabled. Rename it, change
the cost or the prompt in your Creator Dashboard freely — it's yours now, and the
script finds it again by title.

After that, just:

```
./helper/twitch_redemptions.py
```

Leave it running while you play, and set **Characters Speak On → Channel point
reward** in the mod's options.

## About the token

- Stored in `~/.config/BanjoRecompiled/twitch_redemptions.json`, written with
  owner-only permissions (`0600`). It is deliberately outside this repository so
  it cannot be committed by accident.
- Never logged, and never sent anywhere except back to Twitch.
- The scopes requested are the two the job needs and nothing more:
  `channel:read:redemptions` to see redemptions, `channel:manage:redemptions` to
  create the reward and mark them fulfilled.
- `--reauthorise` discards the stored token and starts over. To revoke access
  entirely, remove the app under *Connections* in your Twitch settings.

Only the redeemer's display name and the text they typed are passed to the game.
Nothing is written to disk, and nothing is kept after the message scrolls away.

## Testing without an Affiliate account

You do not need channel points, or even a Twitch account, to test this. Twitch's
own CLI ships a mock API server that implements the channel points rewards and
redemptions endpoints locally.

Download the [Twitch CLI](https://github.com/twitchdev/twitch-cli/releases), then:

```
twitch mock-api generate          # makes fake users, rewards and redemptions
twitch mock-api start             # serves them on http://localhost:8080/mock
```

`generate` prints a Client-ID and a user id that has "all applicable units".
Mint a token for that user:

```
curl -X POST "http://localhost:8080/auth/authorize?client_id=<id>&client_secret=<secret>\
&grant_type=user_token&user_id=<user id>&scope=channel%3Aread%3Aredemptions+channel%3Amanage%3Aredemptions"
```

Then point this script at the mock instead of Twitch:

```
./twitch_redemptions.py --api-base http://localhost:8080/mock \
    --token <the token> --broadcaster-id <user id> \
    --reward-title "Fake reward for <user id>"
```

The generated redemptions have no message text, since the fake reward does not
ask for any. Give a couple of them something to say by editing the CLI's database
directly:

```
sqlite3 ~/.config/twitch-cli/eventCache.db \
  "update channel_points_redemptions set user_input='mumbo: hello'
   where id=(select id from channel_points_redemptions
             where redemption_status='UNFULFILLED' limit 1);"
```

Setting them back to `UNFULFILLED` the same way lets you replay a test.

### What the mock does not cover

Driving redemptions through the mock exercises the polling loop, the socket to
the mod, and the game rendering them. It does not exercise:

- the OAuth device flow, since the mock mints tokens its own way
- creating the reward, since testing uses one the mock generated
- looking the broadcaster up from the token
- refreshing an expired token
- real rate limits

Those paths are written but unproven, and are the most likely place for a first
run against a live account to go wrong.

## Why polling instead of EventSub

`GET /helix/channel_points/custom_rewards/redemptions` needs only HTTPS and JSON,
both of which Python has built in. EventSub would mean a WebSocket client on top
of TLS for no functional gain at this scale — one poll every two seconds is far
inside the rate limit, and a redemption showing up a second or two later is
invisible to the viewer.

The trade-off: that endpoint only returns redemptions for rewards **created by
the same client id**, which is why the script creates its own reward rather than
letting you point it at one you already made. Supporting arbitrary existing
rewards would mean switching to EventSub.

## Why a separate process at all

The alternative is putting TLS, JSON and a credential store inside
`banjo_chatooie_native.so`, a C++ library that has to ship prebuilt for Linux, Windows and
macOS. That is a lot of surface area, and a lot of things to keep patched, in
return for saving you one terminal window.

## Troubleshooting

The mod logs `redemption helper connected on port 47474` when the two find each
other. If you never see it, check the script is running and that nothing else has
taken that port — both sides accept a different one (`--port`, and
`REDEMPTION_PORT` in `src/main.c`).

If Twitch returns 401 repeatedly, the refresh token has been revoked; run with
`--reauthorise`.
