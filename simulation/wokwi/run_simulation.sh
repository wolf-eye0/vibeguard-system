#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ENV_DIR="$SCRIPT_DIR/../VSCode_Wokwi_Environment"

echo "=================================================="
echo "  Launching VibeGuard Wokwi Simulation in VS Code"
echo "=================================================="

"$ENV_DIR/VSCode-linux-x64/bin/code" \
  --extensions-dir "$ENV_DIR/extensions" \
  --user-data-dir "$ENV_DIR/user-data" \
  "$SCRIPT_DIR" "$SCRIPT_DIR/diagram.json"

