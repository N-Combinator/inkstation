#!/usr/bin/env bash
# run_host_tests.sh — compile and run the rtt_parse unit tests on the build host.
# No InkView / libcurl required: the parser only uses cJSON and standard C.
# Run from the repo root:
#   bash tests/run_host_tests.sh
set -euo pipefail

cd "$(dirname "$0")/.."

CC="${CC:-gcc}"
CFLAGS="-std=c11 -Wall -Wextra -fsanitize=address,undefined -g"
OUT="/tmp/test_rtt_host"

echo "Compiling test_rtt (host, ASan/UBSan)..."
$CC $CFLAGS -Isrc \
    tests/test_rtt.c \
    src/rtt.c \
    src/cJSON.c \
    -o "$OUT"

echo "Running..."
"$OUT"

# ---- UI helpers (header corner buttons, key mapping) --------------------
# ui.c needs InkView declarations; generate a minimal stub inkview.h carrying
# the device's real IV_KEY_* codes so a key-code regression would be caught.
INC="$(mktemp -d)"
trap 'rm -rf "$INC"' EXIT
cat > "$INC/inkview.h" <<'HDR'
#ifndef INKVIEW_H
#define INKVIEW_H
#include <stddef.h>
typedef struct ifont ifont;
#define BLACK 0x00
#define DGRAY 0x55
#define LGRAY 0xAA
#define WHITE 0xFF
#define ALIGN_LEFT 0
#define ALIGN_CENTER 1
#define ALIGN_RIGHT 2
#define VALIGN_TOP 0
#define VALIGN_MIDDLE 8
#define VALIGN_BOTTOM 16
enum {
    IV_KEY_OK    = 0x0a,
    IV_KEY_UP    = 0x11,
    IV_KEY_DOWN  = 0x12,
    IV_KEY_LEFT  = 0x13,
    IV_KEY_RIGHT = 0x14,
    IV_KEY_MENU  = 0x17,
    IV_KEY_PREV  = 0x18,
    IV_KEY_NEXT  = 0x19,
    IV_KEY_HOME  = 0x1a,
    IV_KEY_BACK  = 0x1b,
    IV_KEY_PREV2 = 0x1c,
    IV_KEY_NEXT2 = 0x1d
};
int ScreenWidth(void);
int ScreenHeight(void);
ifont *OpenFont(const char *name, int size, int aa);
void CloseFont(ifont *f);
void SetFont(ifont *f, int color);
void DrawTextRect(int x, int y, int w, int h, const char *s, int flags);
void DrawLine(int x1, int y1, int x2, int y2, int color);
void DrawRect(int x, int y, int w, int h, int color);
void FillArea(int x, int y, int w, int h, int color);
void FullUpdate(void);
#endif
HDR

OUT_UI="/tmp/test_ui_host"
echo "Compiling test_ui (host, ASan/UBSan)..."
$CC $CFLAGS -I"$INC" -Isrc \
    tests/test_ui.c \
    src/ui.c \
    -o "$OUT_UI"

echo "Running..."
"$OUT_UI"
