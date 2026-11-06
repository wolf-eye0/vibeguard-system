#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ENV_DIR="$SCRIPT_DIR/../VSCode_Wokwi_Environment"

echo "=================================================="
echo "  VibeGuard: Compiling ESP32 Firmware Locally"
echo "=================================================="

"$ENV_DIR/bin/arduino-cli" compile \
  --fqbn esp32:esp32:esp32 \
  --config-file "$ENV_DIR/config/arduino-cli.yaml" \
  --output-dir "$SCRIPT_DIR/build" \
  "$SCRIPT_DIR/VibeGuard_ESP32"

echo "=================================================="
echo "  Build SUCCESSFUL! Output binary ready in build/"
echo "=================================================="
