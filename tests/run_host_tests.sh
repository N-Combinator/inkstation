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
