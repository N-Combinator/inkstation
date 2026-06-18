# InkStation

Native PocketBook app showing live UK train **Departures** and **Arrivals** from the [Realtime Trains](https://www.realtimetrains.co.uk/) API. Runs on stock firmware via the official InkView SDK — no KOReader, no jailbreak.

## Features

- Offline station search — 2 600+ GB National Rail stations bundled, no network needed to find a station
- Live departure and arrival boards (time, destination/origin, platform, expected time, operator)
- Toggle between Departures and Arrivals in one tap / one key press
- Refresh button + hardware OK key
- WiFi keep-alive: recovers silently from firmware idle-timer power-downs
- On-screen Back button (works on touch-only PocketBook models)

## Requirements

- PocketBook e-reader with stock firmware (tested on InkView SDK 6.3.0 target)
- [Realtime Trains API](https://www.realtimetrains.co.uk/about/developer/pull/docs/) account (free tier available) — provides a `rttapi_*` username and password

## Build

Cross-compile with the [PocketBook SDK 6.3.0](https://github.com/pocketbook/SDK_6.3.0) (`6.5` branch → `SDK-B288/`). The toolchain binaries are Linux x86_64 only.

```bash
cmake -B build \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-arm-obreey.cmake \
      -DPB_SDK_ROOT=/path/to/SDK-B288
cmake --build build
# Produces: build/inkstation.app
```

## Deploy

`deploy.sh` is a one-command **pull → build → install** for the device, with a
flag to choose wired (USB) or wireless (WiFi). It defaults the project dir to its
own location and the SDK to `$PB_SDK_ROOT` (or `~/pocketbook-sdk/SDK-B288`).

```bash
./deploy.sh                            # wired: build + copy over USB (default)
./deploy.sh --usb --pull               # git pull, clean rebuild, copy over USB
./deploy.sh --wifi --find              # wireless: build + install to auto-detected device
./deploy.sh --wifi --ip 192.168.1.42   # wireless: build + install to a known IP
./deploy.sh --build-only               # just cross-compile, don't install
```

Or via the `Makefile`: `make deploy`, `make deploy-wifi IP=...`, `make build`, `make test`.

### Wired (USB)

1. Connect the reader over USB and allow storage access on the device.
2. `./deploy.sh --usb` — builds, then copies `build/inkstation.app` into the
   device's mounted `applications/` folder (auto-detected under `/media/$USER/*/`).
3. Safely eject, then launch **InkStation** from the Applications menu.

### Wireless (WiFi)

The wireless mode installs over the network. You must first set up a **WiFi drop
point** on the reader — this is what receives the binary:

> **Set up the drop point first.** Install [**inkshelf**](https://github.com/N-Combinator/inkshelf)
> on the reader and open its **WiFi Book Drop** screen — that is the WiFi-drop
> server InkStation deploys through (it advertises over mDNS so `--find` can
> locate the device). See the inkshelf README for setup and the PIN.

Two methods:

```bash
# (default) SCP the binary to applications/inkstation.app and relaunch.
# Installs InkStation as its own app. Needs sshd on the reader (PocketBook
# jailbreak / PBJB sshd).
./deploy.sh --wifi --find            # or: --ip <device-ip>
./deploy.sh --wifi --ip 192.168.1.42 --ssh

# Push to inkshelf's WiFi-drop HTTP endpoint (parity with inkshelf's tooling).
./deploy.sh --wifi --http-drop --ip 192.168.1.42 --pin 1234
```

> **`--http-drop` caveat.** inkshelf's `/deploy` endpoint writes the upload to
> `applications/inkshelf.app` (it is inkshelf's own self-update path), so it
> **replaces inkshelf** with the InkStation binary rather than installing a
> separate `inkstation.app`. To run InkStation *alongside* inkshelf, use the
> default `--ssh` method (or install once over USB). `--http-drop` is provided
> for parity with inkshelf's deploy tooling.

## Manual installation

If you prefer not to use `deploy.sh`: copy `build/inkstation.app` to the
PocketBook SD card under `applications/`, eject, and launch **InkStation** from
the Applications menu.

## Configuration

InkStation reads its configuration from:

```
/mnt/ext1/system/config/inkstation.conf
```

Create this file on the SD card (or let InkStation create the directory structure on first run) with your RTT API credential:

```
rtt_refresh_token=rttapi_<username>:<password>
```

The credential is stored only on the device and never leaves it. The `rttapi_` prefix is the standard RTT API username format; the full value is `username:password` sent as HTTP Basic auth.

To regenerate the bundled station list (developer only):

```bash
RTT_REFRESH_TOKEN=rttapi_<user>:<pass> python3 tools/gen_stations.py > src/stations.h
```

## Usage

1. Launch InkStation — a station search screen opens.
2. **Tap the search bar** (or press OK) to type a station name. Results filter as you type over 2 600+ GB National Rail stations.
3. **Tap a station** (or navigate with arrow keys + OK) to open its live board.
4. On the board screen:
   - **"Show Arrivals" / "Show Departures"** button or ← / → keys to toggle mode
   - **"Refresh"** button or OK key to reload from the API
   - **Back** button or Back key to return to search

## Host parser tests

The RTT JSON parser (`rtt_parse`) can be tested on the build host without the PocketBook SDK:

```bash
bash tests/run_host_tests.sh
```

Requires GCC with AddressSanitizer/UBSanitizer (standard on Linux).

## Project structure

```
src/
  main.c          InkView entry point
  app.c/h         Navigation stack
  ui.c/h          Drawing helpers, list widget, key classification
  config.c/h      On-device key=value settings store
  rtt.c/h         Realtime Trains API client (token auth + board parser)
  screens.c/h     Station search and live board screens
  http.c/h        libcurl GET wrapper (WiFi retry, logging)
  net.c/h         WiFi keep-alive for PocketBook firmware
  cJSON.c/h       Embedded JSON parser
  stations.h      Bundled offline GB National Rail station list (generated)
cmake/
  toolchain-arm-obreey.cmake   PocketBook cross-compile toolchain
tools/
  gen_stations.py   Regenerates stations.h from the RTT API
tests/
  test_rtt.c        rtt_parse unit tests (host, no SDK needed)
  run_host_tests.sh Test runner
deploy.sh           one-command pull/build/install (USB or WiFi)
Makefile            convenience targets wrapping deploy.sh
```
