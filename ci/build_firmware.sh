#!/bin/sh
# BUILDS ALL FIRMWARE VARIANTS OF THE PICO NETWORK MODULE AND THE MEGA AND COLLECTS THEM IN ONE FOLDER
# USED BY ci/Dockerfile, ALSO RUNS WITHOUT DOCKER IF PlatformIO (pio) IS INSTALLED
# USAGE: ci/build_firmware.sh [OUTPUT DIR, DEFAULT dist]
set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${1:-$ROOT/dist}"
PICO_DIR="$ROOT/src/picowinterface"
MEGA_DIR="$ROOT/src/bu190t"
PICO_ENVS="rpipicow rpipico2w rpipico2"
MEGA_ENVS="megaatmega2560"

# VERSION FROM #define VERSION "x.y.z" IN THE FIRMWARE SOURCE
fw_version() {
  sed -n 's/^#define VERSION "\(.*\)".*/\1/p' "$1/src/main.cpp" | head -n 1
}

PICO_VERSION="$(fw_version "$PICO_DIR")"
MEGA_VERSION="$(fw_version "$MEGA_DIR")"
mkdir -p "$OUT"

for env in $PICO_ENVS; do
  echo "=== PICO $env $PICO_VERSION ==="
  pio run -d "$PICO_DIR" -e "$env"
  # .uf2 FOR THE BOOTSEL DRIVE, .bin FOR THE WEB UPDATE (/update)
  cp "$PICO_DIR/.pio/build/$env/firmware.uf2" "$OUT/dbclock-pico-$env-$PICO_VERSION.uf2"
  cp "$PICO_DIR/.pio/build/$env/firmware.bin" "$OUT/dbclock-pico-$env-$PICO_VERSION.bin"
done

for env in $MEGA_ENVS; do
  echo "=== MEGA $env $MEGA_VERSION ==="
  pio run -d "$MEGA_DIR" -e "$env"
  cp "$MEGA_DIR/.pio/build/$env/firmware.hex" "$OUT/dbclock-mega-$env-$MEGA_VERSION.hex"
done

{
  echo "pico network module: $PICO_VERSION ($PICO_ENVS)"
  echo "mega: $MEGA_VERSION ($MEGA_ENVS)"
  echo "source: ${FIRMWARE_REF:-unknown}"
  echo "built: $(date -u '+%Y-%m-%d %H:%M:%S UTC')"
} > "$OUT/BUILDINFO.txt"

cd "$OUT"
sha256sum dbclock-* > SHA256SUMS
cat BUILDINFO.txt SHA256SUMS
