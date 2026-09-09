# InkStation (why not?)

[![Latest release](https://img.shields.io/github/v/release/N-Combinator/inkstation)](https://github.com/N-Combinator/inkstation/releases/latest)
![Downloads](https://img.shields.io/github/downloads/N-Combinator/inkstation/total)

Native PocketBook app showing live UK train **Departures** and **Arrivals** from the [Realtime Trains](https://www.realtimetrains.co.uk/) API. Runs on stock firmware via the official InkView SDK — no KOReader, no jailbreak.

**Contents**

- [Screenshots](#screenshots)
- [Install](#install)
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

1. Download `inkstation-<version>.zip` from the
   [latest release](https://github.com/N-Combinator/inkstation/releases/latest)
   and unzip it — inside is a single file, `inkstation.app`. (It ships zipped
   because GitHub refuses release assets with an `.app` extension.)
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

Optionally verify the download against the `SHA256SUMS.txt` published next to
the zip — it covers both the archive and the `inkstation.app` inside it, so run
it from the folder holding the downloaded zip and the unzipped binary:

```bash
sha256sum -c SHA256SUMS.txt
```

Every release binary is built by
[GitHub Actions](.github/workflows/release.yml) from the tagged source, so the
build log for the exact file you downloaded is public under the repo's Actions
tab.

**Device support.** The binary is an ARM 32-bit ELF built against the official
SDK (`SDK-B288`, i.e. SDK_6.3.0 branch `6.5`) and targets stock PocketBook
firmware. It will **not** run on the newer 64-bit models — the InkPad One and
anything else on the Rockchip RK3566 platform — because there is no public
InkView SDK for that architecture yet; KOReader hit the same wall on those
devices. If it works, or fails to launch, on your model, please open an issue
naming the model and firmware version so this section can list what is actually
verified.

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
- On-screen Back button (works on touch-only PocketBook models)

## Usage

1. Launch InkStation — a station search screen opens.
2. **Tap the search bar** (or press OK) to type a station name. Results filter as you type over 2 600+ GB National Rail stations.
3. **Tap a station** (or navigate with arrow keys + OK) to open its live board.
4. On the board screen:
   - **"Show Arrivals" / "Show Departures"** button or ← / → keys to toggle mode
   - **"Refresh"** button or OK key to reload from the API
   - **Back** button or Back key to return to search

## Contributing

Build instructions, the deploy tooling, the host test gate, the project layout
and the release process live in **[BUILDING.md](BUILDING.md)**.

## License

MIT.
