# Building InkStation from source

**Most people do not need this page.** Every release ships a prebuilt
`inkstation.app`; see [Install](README.md#install). Build from source if you want
to change the code, or to check what you are installing.

InkStation cross-compiles with the `arm-obreey-linux-gnueabi` toolchain from the
official [PocketBook SDK_6.3.0](https://github.com/pocketbook/SDK_6.3.0). The
output is always a single `build/inkstation.app` (an ARM 32-bit ELF).

**Contents**

- [Get the SDK](#1-get-the-sdk-on-the-65-branch-not-master)
- [Build and install with deploy.sh](#2-build-and-install-with-deploysh)
- [Manual CMake](#3-manual-cmake)
- [Provisioning the RTT token](#provisioning-the-rtt-token)
- [Host tests](#host-tests)
- [Regenerating the station list](#regenerating-the-station-list)
- [Project layout](#project-layout)
- [RK3566 build (InkPad One)](#rk3566-build-inkpad-one)
- [Cutting a release](#cutting-a-release)

## 1. Get the SDK (on the `6.5` branch, not `master`)

The SDK repo's default `master` branch contains **only a README** — the actual
SDK lives on the `6.5` branch under `SDK-B288/`.

```bash
git clone --depth 1 --single-branch --branch 6.5 \
  https://github.com/pocketbook/SDK_6.3.0 ~/pocketbook-sdk   # SDK-B288/ is now there
```

(A plain `git clone` pulls ~680 MB because it downloads every branch's objects;
the flags above fetch only the one branch at one revision.)

The compiler is `SDK-B288/usr/bin/arm-obreey-linux-gnueabi-gcc` and the InkView
sysroot is `SDK-B288/usr/arm-obreey-linux-gnueabi/sysroot`. **The toolchain
binaries are Linux x86_64 ELF** — they run on a Linux x86_64 host only (not
natively on macOS; use a `linux/amd64` container there).

`deploy.sh` expects the SDK at `~/pocketbook-sdk/SDK-B288`; override with
`PB_SDK_ROOT=/path/to/SDK-B288`.

## 2. Build and install with `deploy.sh`

`deploy.sh` is the one-command **pull → build → install** path, with a flag to
choose wired (USB) or wireless (WiFi). It defaults the project dir to its own
location, so it works from any clone.

```bash
./deploy.sh                            # wired: build + copy over USB (default)
./deploy.sh --usb --pull               # git pull, clean rebuild, copy over USB
./deploy.sh --wifi --find              # wireless: build + install to auto-detected device
./deploy.sh --wifi --ip 192.168.1.42   # wireless: build + install to a known IP
./deploy.sh --build-only               # just cross-compile, don't install
```

Or via the `Makefile`: `make deploy`, `make deploy-wifi IP=...`, `make build`,
`make test`.

It configures a **Release** build by default (without a build type CMake compiles
at `-O0` and keeps every symbol); `CMAKE_BUILD_TYPE=Debug ./deploy.sh --build-only`
if you want symbols. `--build-only` is what the release workflow runs.

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

## 3. Manual CMake

```bash
cmake -B build \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-arm-obreey.cmake \
      -DCMAKE_BUILD_TYPE=Release \
      -DPB_SDK_ROOT=/path/to/SDK-B288
cmake --build build
# Produces: build/inkstation.app
```

Driving `cmake` yourself rather than through `deploy.sh`, you may hit:

```
cc1: error while loading shared libraries: libmpfr.so.4
```

The SDK's compiler is a 2017 gcc 6.3 that wants mpfr 3.x, and distros have
shipped `libmpfr.so.6` for years with no compatible `.so.4` to install. The SDK
carries the right library itself, so point the loader at it — and only at the
libraries the compiler needs, because `$PB_SDK_ROOT/usr/lib` also holds 2017
builds of glib/icu/expat that would shadow your host's:

```bash
mkdir -p .pb-hostlibs
for lib in libmpfr.so.4 libmpc.so.3 libgmp.so.10; do
  ln -sfn "$PB_SDK_ROOT/usr/lib/$lib" ".pb-hostlibs/$lib"
done
export LD_LIBRARY_PATH="$PWD/.pb-hostlibs:$LD_LIBRARY_PATH"
```

`deploy.sh` does this for you. Keep the directory outside `build/`: `deploy.sh`
configures only when `build/` does not exist yet, so creating something under it
first would silently skip configuration.

## Provisioning the RTT token

InkStation reads its token at startup from `/mnt/ext1/system/config/inkstation.conf`
on the device, one line:

```
rtt_refresh_token=<your RTT refresh token>
```

`deploy.sh` writes that file for you, so you don't have to create it by hand.
Supply the token one of three ways (checked in order):

```bash
./deploy.sh --usb --rtt-token 'eyJ...'               # explicit flag
RTT_TOKEN='eyJ...' ./deploy.sh --usb                 # environment variable
echo 'eyJ...' > rtt_token.txt && ./deploy.sh --usb   # gitignored local file
```

`rtt_token.txt` is in `.gitignore` and never leaves your machine. The same
applies to `--wifi --ssh` deploys (the token is written over SSH). The
`--http-drop` method cannot write the token — set it via USB/SSH or by hand.

Where the token comes from: [README → Getting an API token](README.md#getting-an-api-token).

## Host tests

The RTT JSON parser (`rtt_parse`) is covered by a host test gate that needs
**neither the PocketBook SDK nor a network connection**:

```bash
make test            # or: bash tests/run_host_tests.sh
```

Requires GCC with AddressSanitizer/UBSanitizer (standard on Linux). The same
gate runs in CI on every push and pull request, and again before any release
binary is built.

## Regenerating the station list

```bash
RTT_REFRESH_TOKEN='<token>' python3 tools/gen_stations.py > src/stations.h
```

## Project layout

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
deploy.sh           one-command pull/build/install (USB or WiFi); PB_PLATFORM=rk3566 for InkPad One
tools/stage-rk3566-sdk.sh  extract + verify the RK3566 SDK files the armhf build links against
Makefile            convenience targets wrapping deploy.sh
.github/workflows/  CI: test + cross-compile + publish the release binary
```

## RK3566 build (InkPad One)

The RK3566 readers run 32-bit ARM code with the **hard-float** ABI and ship only
`/lib/ld-linux-armhf.so.3`, so they need their own build. It is made differently
from the B288 one, on purpose:

- **Compiler:** the distribution's `arm-linux-gnueabihf-gcc`, not the toolchain
  inside PocketBook's SDK. That SDK (6.11) is only available as a third-party
  re-upload ([Sean-on-Git/PocketBook-SDK](https://github.com/Sean-on-Git/PocketBook-SDK/releases/tag/6.11)),
  so nothing from it is allowed to run on the build machine.
- **From the SDK we take 18 files and nothing else:** `inkview.h`, `hwconfig.h`,
  curl's and zlib's headers, and `libinkview.so` + `libcurl.so` to link against.
  The libraries are consulted only for symbol names; the reader loads its own
  firmware copies at runtime, so nothing from them ends up in `inkstation.app`.
- **Everything is pinned.** `tools/stage-rk3566-sdk.sh` refuses an archive whose
  SHA-256 differs from the audited one and checks every extracted file against
  its own pinned hash. Its header records what was verified before pinning: curl
  headers byte-identical to the GPG-signed 8.16.0 release, zlib headers to 1.3.1,
  no inline code or process/network calls in the InkView headers, and the whole
  tree scanned.

Locally, on Debian/Ubuntu — on any host architecture, since the distribution
compiler also exists for arm64:

```bash
sudo apt install gcc-arm-linux-gnueabihf 7zip        # p7zip-full on older releases
curl -fLO https://github.com/Sean-on-Git/PocketBook-SDK/releases/download/6.11/SDK-RK3566-6.11.7z
tools/stage-rk3566-sdk.sh SDK-RK3566-6.11.7z ~/pb-rk3566-stage
PB_PLATFORM=rk3566 PB_HF_STAGE=~/pb-rk3566-stage ./deploy.sh --build-only   # -> build-rk3566/inkstation.app
```

`PB_PLATFORM=rk3566` works with every `deploy.sh` mode, so
`PB_PLATFORM=rk3566 PB_HF_STAGE=... ./deploy.sh --usb` builds and installs the
RK3566 binary. It selects `cmake/toolchain-armhf.cmake` and its own
`build-rk3566/` directory, never reusing the B288 build's CMake cache, and it
refuses to install a binary that is not hard-float.

## Cutting a release

`.github/workflows/release.yml` does the whole thing on a `v*` tag — nobody has
to build a release binary by hand:

```bash
git tag v1.1.0
git push origin v1.1.0
```

The workflow then

1. runs the host test gate (a failure here stops the release; no binary ships),
2. builds both platforms in parallel:
   - **B288** — shallow clone of the SDK's `6.5` branch (cached, keyed on its head
     commit), cross-compiled via `deploy.sh --build-only`, refused unless
     `readelf` reports a soft-float binary that uses `/lib/ld-linux.so.3`;
   - **RK3566** — the distribution's armhf compiler plus the pinned, verified
     files from `tools/stage-rk3566-sdk.sh`, refused unless the binary is
     hard-float, uses `/lib/ld-linux-armhf.so.3`, and links against nothing but
     libc, InkView and curl;
3. publishes `inkstation-<tag>-b288.zip`, `inkstation-<tag>-rk3566.zip` and a
   merged `SHA256SUMS.txt`, creating the release if the tag has none yet. The
   binaries are zipped because GitHub rejects release assets whose name ends in
   `.app`.

Publishing is a separate job: it is the only one with write access, and it runs
no SDK tooling — it just uploads what the build jobs produced. The build jobs get
a read-only token.

A tag with a suffix — `v1.2.0-rc1` — is published as a **pre-release**, so a
test build can be handed to someone without becoming the "Latest release" that
the README's install link points at.

Writing release notes first is fine: if a release for the tag already exists, the
workflow keeps its notes and only attaches the binaries.

Pull requests that touch the source or the build run the same two builds, without
publishing, so a change that breaks one platform shows up before it is merged.

The workflow can also be started by hand from the Actions tab (*Run workflow*).
Leave the `tag` input empty to build whatever branch you picked and get the zips
as workflow artifacts. Give it a tag and it checks that tag out, builds it, and
attaches the binaries to its release. A tag older than the RK3566 build simply
gets the B288 zip: the RK3566 job notices the missing toolchain file and skips.
The branch chosen in *Use workflow from* only decides which version of the
workflow runs; `tag` decides what gets built.

### Versioning

`CMakeLists.txt` stamps the binary with `git describe --tags --match "v*"`, and
the app shows that string. So the version users see comes from the tag: tagged
builds read `v1.1.0`, builds ahead of a tag read `v1.1.0-3-gabc1234`, and a build
outside a git checkout falls back to `dev`. Tag before you build a binary you
intend to hand to someone.
