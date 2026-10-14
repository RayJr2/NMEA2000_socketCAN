#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
core=${1:?Pass the verified NMEA2000 core directory}
out=${2:?Pass a disposable output directory}
mkdir -p "$out"
wrap=()
for op in socket ioctl bind fcntl close read write select; do wrap+=("-Wl,--wrap=$op"); done
g++ -std=c++17 -g -O1 -Wall -Wextra -Wpedantic -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I. -I"$core/src" tests/lifetime.cpp NMEA2000_SocketCAN.cpp \
  "$core/src/NMEA2000.cpp" "$core/src/N2kMsg.cpp" "$core/src/N2kMessages.cpp" \
  "$core/src/Seasmart.cpp" "$core/src/N2kStream.cpp" "$core/src/N2kTimer.cpp" "$core/src/N2kGroupFunction.cpp" \
  "$core/src/N2kGroupFunctionDefaultHandlers.cpp" "${wrap[@]}" -o "$out/lifetime"
ASAN_OPTIONS=detect_leaks=1 "$out/lifetime"
