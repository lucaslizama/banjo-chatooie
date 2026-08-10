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
