# InkStation (why not?)

[![Latest release](https://img.shields.io/github/v/release/N-Combinator/inkstation)](https://github.com/N-Combinator/inkstation/releases/latest)
![Downloads](https://img.shields.io/github/downloads/N-Combinator/inkstation/total)

Native PocketBook app showing live UK train **Departures** and **Arrivals** from the [Realtime Trains](https://www.realtimetrains.co.uk/) API. Runs on stock firmware via the official InkView SDK — no KOReader, no jailbreak.

**Contents**

- [Screenshots](#screenshots)
- [Install](#install)
- [Which file do I need?](#which-file-do-i-need)
- [Getting an API token](#getting-an-api-token)
- [Features](#features)
- [Usage](#usage)
- [Building from source](BUILDING.md)

## Screenshots

<img src="screenshots/inkstation_demo.jpg" alt="InkStation showing live Guildford departures on a PocketBook e-reader" width="320">

*Live Guildford departures on a PocketBook, straight from the Realtime Trains API.*

## Install

**You do not need to build anything.** Every release ships a ready-to-run
`inkstation.app`; the source build is only for people who want to change the
code (see [BUILDING.md](BUILDING.md)).

1. Download the zip for your reader from the
   [latest release](https://github.com/N-Combinator/inkstation/releases/latest)
   — `inkstation-<version>-b288.zip` for most PocketBooks, or
   `inkstation-<version>-rk3566.zip` for the InkPad One and other RK3566 models
   (see [Which file do I need?](#which-file-do-i-need)). Unzip it: inside is a
   single file, `inkstation.app`. (It ships zipped because GitHub refuses release
   assets with an `.app` extension.)
2. Connect the reader over USB (or pull its SD card) and copy `inkstation.app`
   into the `applications/` folder of the storage the reader exposes.
3. **Add your API token** — the app needs one to show any board. In the same
   storage, create `system/config/inkstation.conf` (on the device this path is
   `/mnt/ext1/system/config/inkstation.conf`) containing one line:

   ```
   rtt_refresh_token=<your RTT refresh token>
   ```

   Without it, boards show *"No RTT token set"*. See
   [Getting an API token](#getting-an-api-token).
4. Eject the reader and launch **InkStation** from its Applications menu.

That is the whole install. A PocketBook `.app` is a plain ARM executable that
the launcher runs — there is no signing, no store, no firmware change, and
uninstalling is deleting the file. The token lives only on your device: it is
never committed to this repo or baked into the binary.

Optionally verify the download against the `SHA256SUMS.txt` published with the
release. It lists both zips, so tell `sha256sum` to skip the one you did not
download:

```bash
sha256sum -c --ignore-missing SHA256SUMS.txt
```

The same file carries, as a comment, the hash of the `inkstation.app` inside each
zip — if you also want to check the file that lands on the reader.

Every release binary is built by
[GitHub Actions](.github/workflows/release.yml) from the tagged source, so the
build log for the exact file you downloaded is public under the repo's Actions
tab.

### Which file do I need?

PocketBook readers come in two userland ABIs, and a binary built for one cannot
even be loaded on the other — the launcher simply does nothing.

| Reader | Download |
|---|---|
| **InkPad One** (PB1030) and other models on the Rockchip **RK3566** platform | `inkstation-<version>-rk3566.zip` |
| Every other PocketBook on stock firmware | `inkstation-<version>-b288.zip` |

Why: the RK3566 readers pair a 64-bit kernel with a 32-bit ARM **hard-float**
userland that only ships `/lib/ld-linux-armhf.so.3`. The classic build is
soft-float and asks for `/lib/ld-linux.so.3`, which is not there. So it is not a
64-bit problem, and the RK3566 build is still a 32-bit ARM binary.

Not sure which one you have? Try `b288` first; if InkStation does not start,
delete it and copy the `rk3566` one instead. Nothing gets installed either way,
so picking the wrong one is harmless. Release v1.0.0 contains only the B288
build.

**Status of the RK3566 build:** built against PocketBook's SDK 6.11 and checked
to be a hard-float binary, but not yet run on a device by the maintainers — that
includes the token file location, which is assumed to be the same
`system/config/` path. If you try it, please open an issue saying whether it
starts, with your model and firmware version.

## Getting an API token

InkStation talks to the [Realtime Trains **Next Generation**
API](https://api-portal.rtt.io/). Sign up there and take the **refresh token** —
a Bearer JWT starting `eyJ...`. InkStation exchanges it for a short-lived access
token automatically and caches that on the device.

(This is *not* the older `api.rtt.io/api/v1` Basic-auth API; that one is unused.)

If you build from source, `deploy.sh` can write the config file to the device
for you — see [BUILDING.md](BUILDING.md).

## Features

- Offline station search — 2 600+ GB National Rail stations bundled, no network needed to find a station
- Live departure and arrival boards (time, destination/origin, platform, expected time, operator)
- Toggle between Departures and Arrivals in one tap / one key press
- Refresh button + hardware OK key
- WiFi keep-alive: recovers silently from firmware idle-timer power-downs
- On-screen Back and Exit buttons (work on touch-only PocketBook models)

## Usage

1. Launch InkStation — a station search screen opens.
2. **Tap the search bar** (or press OK) to type a station name. Results filter as you type over 2 600+ GB National Rail stations.
3. **Tap a station** (or navigate with arrow keys + OK) to open its live board.
4. On the board screen:
   - **"Show Arrivals" / "Show Departures"** button or ← / → keys to toggle mode
   - **"Refresh"** button or OK key to reload from the API
   - **Back** button or Back key to return to search
5. On the search screen, the **Exit** button (top-right) or the Back key closes InkStation.

## Contributing

Build instructions, the deploy tooling, the host test gate, the project layout
and the release process live in **[BUILDING.md](BUILDING.md)**.

## License

MIT.
