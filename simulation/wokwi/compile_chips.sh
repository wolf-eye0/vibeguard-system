#!/usr/bin/env bash
set -euo pipefail

# ==============================================================================
# VibeGuard Custom Wokwi Chip Compilation Script
# Compiles C chip sources into WebAssembly (.wasm) via local WASI SDK Clang
# ==============================================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ENV_DIR="${SCRIPT_DIR}/../VSCode_Wokwi_Environment"
CLANG="${CLANG:-${ENV_DIR}/wasi-sdk-24.0-x86_64-linux/bin/clang}"

if [ ! -x "${CLANG}" ]; then
  echo "[ERROR] WASI SDK Clang compiler not found at: ${CLANG}" >&2
  exit 1
fi

echo "=============================================================================="
echo "  VibeGuard: Compiling Custom Wokwi Chips (WebAssembly WASI SDK 24.0)"
echo "  Compiler: ${CLANG}"
echo "=============================================================================="

CFLAGS=(
  --target=wasm32-unknown-wasi
  -nostartfiles
  -Wl,--import-memory
  -Wl,--export-table
  -Wl,--no-entry
  -Wl,-z,stack-size=8192
  -Wextra
  -O3
  -I"${SCRIPT_DIR}"
)

CHIPS=(
  "adxl345-vibe"
  "n20-motor"
  "pwr-supply-12v"
)

for chip in "${CHIPS[@]}"; do
  src="${SCRIPT_DIR}/${chip}.chip.c"
  out="${SCRIPT_DIR}/${chip}.chip.wasm"
  
  if [ ! -f "${src}" ]; then
    echo "[ERROR] Source file missing: ${src}" >&2
    exit 1
  fi
  
  echo "  --> Compiling ${chip}.chip.c -> ${chip}.chip.wasm ..."
  "${CLANG}" "${CFLAGS[@]}" -o "${out}" "${src}"
  
  size=$(wc -c < "${out}")
  echo "      [OK] ${chip}.chip.wasm (${size} bytes)"
done

echo "=============================================================================="
echo "  All 3 custom chips compiled successfully to WebAssembly!"
echo "=============================================================================="
