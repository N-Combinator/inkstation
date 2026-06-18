#!/usr/bin/env bash
#
# deploy.sh — pull (optional), cross-compile InkStation for PocketBook (ARM),
# and install it on the device, over USB (wired) or over WiFi (wireless).
#
# One script, two modes — pick with --usb (default) or --wifi:
#
#   ./deploy.sh                              # wired: build + copy over USB
#   ./deploy.sh --usb --pull                 # git pull, clean rebuild, copy over USB
#   ./deploy.sh --wifi --find                # wireless: build + SCP to auto-detected device
#   ./deploy.sh --wifi --ip 192.168.1.42     # wireless: build + SCP to a known IP
#   ./deploy.sh --wifi --http-drop --ip <IP> --pin 1234   # via inkshelf WiFi-drop endpoint
#
# WIRED (--usb): copies build/inkstation.app into the mounted device's
#   applications/ folder (device must expose its storage over USB).
#
# WIRELESS (--wifi): installs over the network. Two methods:
#   --ssh        (default) SCP the binary to /mnt/ext1/applications/inkstation.app
#                and relaunch. Installs InkStation as its own app. Needs sshd on
#                the reader (e.g. the PocketBook jailbreak / PBJB sshd).
#   --http-drop  POST the binary to a running inkshelf WiFi-drop server. Requires
#                inkshelf installed and its "WiFi Book Drop" screen open — that is
#                the "drop point". See https://github.com/N-Combinator/inkshelf
#                CAVEAT: inkshelf's /deploy endpoint writes the upload to
#                applications/inkshelf.app (it is inkshelf's own self-update path),
#                so it REPLACES inkshelf with this binary rather than installing a
#                separate inkstation.app. Use --ssh to install InkStation alongside
#                inkshelf. --http-drop is kept for parity with inkshelf's tooling.
#
# The project dir defaults to this script's own directory, so it works from any
# clone. Override the SDK with PB_SDK_ROOT.
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT="${INKSTATION_DIR:-$SCRIPT_DIR}"
SDK="${PB_SDK_ROOT:-$HOME/pocketbook-sdk/SDK-B288}"
APP="$PROJECT/build/inkstation.app"

# WiFi defaults (match inkshelf's WiFi-drop server).
INKSHELF_PORT=8080
DEPLOY_ENDPOINT="/deploy"
SSH_USER="root"
SSH_PORT=22
APP_DEST="/mnt/ext1/applications/inkstation.app"
CONF_DEST="/mnt/ext1/system/config/inkstation.conf"
CONF_KEY="rtt_refresh_token"          # must match config.h CONFIG_KEY_RTT_TOKEN

# ---- defaults / argument parsing --------------------------------------------
MODE="usb"            # usb | wifi
WIFI_METHOD="ssh"     # ssh | http-drop
DO_PULL=0
DO_BUILD=1
DO_FIND=0
BUILD_ONLY=0
PB_IP=""
PIN=""
RTT_TOKEN_ARG=""

usage() {
  sed -n '2,40p' "$0" | sed 's/^# \{0,1\}//'
  exit "${1:-0}"
}

while [ $# -gt 0 ]; do
  case "$1" in
    --usb)        MODE="usb" ;;
    --wifi)       MODE="wifi" ;;
    --ssh)        WIFI_METHOD="ssh" ;;
    --http-drop)  WIFI_METHOD="http-drop" ;;
    --find)       DO_FIND=1 ;;
    --ip)         shift; PB_IP="${1:-}" ;;
    --ip=*)       PB_IP="${1#--ip=}" ;;
    --pin)        shift; PIN="${1:-}" ;;
    --pin=*)      PIN="${1#--pin=}" ;;
    --rtt-token)  shift; RTT_TOKEN_ARG="${1:-}" ;;
    --rtt-token=*) RTT_TOKEN_ARG="${1#--rtt-token=}" ;;
    --pull)       DO_PULL=1; DO_BUILD=1 ;;
    --no-build)   DO_BUILD=0 ;;
    --build-only) BUILD_ONLY=1 ;;
    -h|--help)    usage 0 ;;
    *)            echo "!! unknown argument: $1" >&2; usage 2 ;;
  esac
  shift
done

# ---- auto-detect the device (the running inkshelf WiFi-drop point) -----------
# Prints the device IP on stdout; progress goes to stderr.
find_device() {
  echo ">> scanning local network for the inkshelf WiFi-drop point..." >&2

  # 1) mDNS — inkshelf advertises _inkshelf._tcp while its server is up.
  if command -v avahi-browse >/dev/null 2>&1; then
    local ip
    ip=$(avahi-browse -t -r -p _inkshelf._tcp 2>/dev/null \
         | awk -F';' '/^=/{print $8; exit}')
    if [ -n "${ip:-}" ]; then echo "$ip"; return 0; fi
  fi

  # 2) Probe :PORT across the local /24 for the WiFi Book Drop page.
  local local_ip base tmp ip
  local_ip=$(ip -4 route get 1.1.1.1 2>/dev/null | grep -oP 'src \K\S+' || true)
  if [ -n "${local_ip:-}" ]; then
    base=${local_ip%.*}
    echo ">> probing ${base}.1-254:${INKSHELF_PORT} for the WiFi Book Drop page..." >&2
    tmp=$(mktemp)
    for i in $(seq 1 254); do
      ( curl -s -m 1 "http://${base}.${i}:${INKSHELF_PORT}/" 2>/dev/null \
          | grep -q 'WiFi Book Drop' && echo "${base}.${i}" >>"$tmp" ) &
    done
    wait
    ip=$(head -n1 "$tmp" 2>/dev/null || true); rm -f "$tmp"
    if [ -n "${ip:-}" ]; then echo "$ip"; return 0; fi
  fi

  echo "!! could not find the inkshelf WiFi-drop point on the network." >&2
  echo "   Install inkshelf, open its WiFi Book Drop screen, and retry." >&2
  echo "   https://github.com/N-Combinator/inkshelf" >&2
  return 1
}

# ---- RTT token -------------------------------------------------------------
# The API token is a SECRET and is never committed. Resolve it from, in order:
#   1. --rtt-token <tok>
#   2. $RTT_TOKEN in the environment
#   3. a gitignored rtt_token.txt in the project root (first non-comment line)
# When found, deploy writes it into the device config so the app can read it at
# runtime (config key rtt_refresh_token) — fixing "NO RTT key" when the config
# file was never shipped with the .app. The value may be a Bearer token (JWT) or
# "user:pass" for HTTP Basic; the app picks the scheme by the presence of ':'.
resolve_token() {
  if [ -n "$RTT_TOKEN_ARG" ]; then echo "$RTT_TOKEN_ARG"; return 0; fi
  if [ -n "${RTT_TOKEN:-}" ]; then echo "$RTT_TOKEN"; return 0; fi
  local f="$PROJECT/rtt_token.txt"
  if [ -f "$f" ]; then
    grep -vE '^\s*(#|$)' "$f" | head -n1 | tr -d '[:space:]'
    return 0
  fi
  echo ""
}

# Provision the token into the device config. $1 = "usb:<mount>" or "ssh".
provision_token() {
  local where="$1" token
  token=$(resolve_token)
  if [ -z "$token" ]; then
    echo "!! no RTT token supplied — the app will show 'NO RTT key' until one is set."
    echo "   Provide it with --rtt-token <tok>, \$RTT_TOKEN, or rtt_token.txt, OR"
    echo "   create $CONF_DEST on the device with: $CONF_KEY=<token>"
    return 0
  fi
  case "$where" in
    usb:*)
      local mount="${where#usb:}"
      mkdir -p "${mount}system/config"
      printf '%s=%s\n' "$CONF_KEY" "$token" > "${mount}${CONF_DEST#/mnt/ext1/}"
      sync
      echo ">> wrote RTT token to ${mount}${CONF_DEST#/mnt/ext1/}" ;;
    ssh)
      ssh -p "$SSH_PORT" "$SSH_USER@$PB_IP" \
        "mkdir -p '$(dirname "$CONF_DEST")' && printf '%s=%s\n' '$CONF_KEY' '$token' > '$CONF_DEST'" \
        && echo ">> wrote RTT token to $CONF_DEST on the device" ;;
  esac
}

# ---- build -------------------------------------------------------------------
build_app() {
  export PATH="$SDK/usr/bin:$PATH"
  cd "$PROJECT"

  if [ "$DO_PULL" = 1 ]; then
    echo ">> git pull"
    git pull
    rm -rf build
  fi

  if [ ! -d build ]; then
    echo ">> cmake configure (toolchain: cmake/toolchain-arm-obreey.cmake)"
    cmake -B build \
      -DCMAKE_TOOLCHAIN_FILE="$PROJECT/cmake/toolchain-arm-obreey.cmake" \
      -DPB_SDK_ROOT="$SDK"
  fi

  echo ">> build"
  cmake --build build

  echo ">> binary check:"
  file "$APP"
  file "$APP" | grep -q "ELF 32-bit.*ARM" || { echo "!! not an ARM binary — check the SDK" >&2; exit 1; }
}

# ---- wired (USB) deploy ------------------------------------------------------
deploy_usb() {
  local mount=""
  for d in /media/"$USER"/*/; do
    [ -d "${d}applications" ] && mount="$d" && break
  done

  if [ -z "$mount" ]; then
    echo "!! device not mounted under /media/$USER" >&2
    echo "   Connect over USB, allow storage access on the reader, then re-run." >&2
    exit 1
  fi

  cp "$APP" "${mount}applications/"
  sync
  echo ">> deployed: ${mount}applications/inkstation.app"
  provision_token "usb:${mount}"
  echo ">> safely eject the device, then launch InkStation from Applications."
}

# ---- wireless (WiFi) deploy --------------------------------------------------
deploy_wifi_ssh() {
  echo ">> mode: WiFi / SCP → $SSH_USER@$PB_IP:$APP_DEST"
  scp -P "$SSH_PORT" "$APP" "$SSH_USER@$PB_IP:$APP_DEST"
  provision_token "ssh"
  echo ">> installed. Relaunch from the Applications menu (or via SSH)."
  ssh -p "$SSH_PORT" "$SSH_USER@$PB_IP" \
    "killall inkstation.app 2>/dev/null; true" || true
  echo ">> done."
}

deploy_wifi_http() {
  echo ">> mode: WiFi / HTTP POST → http://$PB_IP:$INKSHELF_PORT$DEPLOY_ENDPOINT"
  echo "!! NOTE: inkshelf's /deploy writes to applications/inkshelf.app — this"
  echo "   REPLACES inkshelf with the InkStation binary. Use --ssh to keep both."
  echo "!! NOTE: this method cannot write the RTT token to the device. Set it via"
  echo "   --usb/--ssh, or create $CONF_DEST with $CONF_KEY=<token> by hand."
  local pin_args=()
  [ -n "$PIN" ] && pin_args=(-H "X-Inkshelf-PIN:$PIN")

  local response http_status body
  response=$(curl -s -w $'\n%{http_code}' \
    -X POST \
    "${pin_args[@]}" \
    -F "file=@$APP;type=application/octet-stream" \
    "http://$PB_IP:$INKSHELF_PORT$DEPLOY_ENDPOINT" \
    --connect-timeout 5 \
    --max-time 120) || true
  http_status=${response##*$'\n'}
  body=${response%$'\n'*}

  case "$http_status" in
    200) echo ">> deployed — device will restart the app automatically"
         [ -n "$body" ] && echo "   device: $body" ;;
    403) echo "!! HTTP 403 — wrong or missing PIN. Pass --pin <number on the reader>." >&2; exit 1 ;;
    000) echo "!! no connection to $PB_IP:$INKSHELF_PORT — reader offline/asleep or" >&2
         echo "   WiFi Book Drop screen not open. Wake it and retry (or --find)." >&2; exit 1 ;;
    *)   echo "!! HTTP $http_status — ${body:-deploy failed}" >&2; exit 1 ;;
  esac
}

# ---- run ---------------------------------------------------------------------
if [ "$DO_BUILD" = 1 ]; then
  build_app
elif [ ! -f "$APP" ]; then
  echo "!! $APP not found — drop --no-build, or build it first" >&2
  exit 1
fi

if [ "$BUILD_ONLY" = 1 ]; then
  echo ">> build-only: skipping install. Binary at $APP"
  exit 0
fi

case "$MODE" in
  usb)
    deploy_usb
    ;;
  wifi)
    if [ "$DO_FIND" = 1 ] && [ -z "$PB_IP" ]; then
      PB_IP=$(find_device)
      echo ">> found device: $PB_IP"
    fi
    if [ -z "$PB_IP" ]; then
      echo "!! no device IP — pass --ip <addr> or --find" >&2
      exit 1
    fi
    if [ "$WIFI_METHOD" = "http-drop" ]; then
      deploy_wifi_http
    else
      deploy_wifi_ssh
    fi
    ;;
esac
