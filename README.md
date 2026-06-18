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

## Installation

1. Copy `inkstation.app` to the PocketBook SD card under `applications/`.
2. Reboot or use the app launcher to find **InkStation**.

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
```
