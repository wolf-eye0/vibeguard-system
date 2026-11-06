#!/bin/bash
echo "[AUTO-FLASHER] Active and waiting for stable connection on /dev/ttyUSB* or /dev/ttyACM*..."
for i in {1..180}; do
  PORT=$(ls /dev/ttyUSB* /dev/ttyACM* 2>/dev/null | head -n 1)
  if [ -n "$PORT" ]; then
    echo "[AUTO-FLASHER] Detected $PORT! Waiting 1 second for connection stabilization..."
    sleep 1
    if [ -e "$PORT" ]; then
      echo "[AUTO-FLASHER] Port is stable! Uploading VibeGuard_Benchmark_Logger..."
      /home/paradoxpete/.local/bin/arduino-cli upload -p "$PORT" --fqbn esp32:esp32:esp32 /home/paradoxpete/Documents/PROJECT_ORGANIZED/VibeGuard_Benchmark_Logger/
      EXIT_CODE=$?
      if [ $EXIT_CODE -eq 0 ]; then
        echo "[AUTO-FLASHER] >>> UPLOAD 100% SUCCESSFUL! <<<"
        exit 0
      else
        echo "[AUTO-FLASHER] Upload attempt failed (exit code $EXIT_CODE). Retrying..."
      fi
    else
      echo "[AUTO-FLASHER] Port was unplugged/loose before upload could begin. Continuing scan..."
    fi
  fi
  sleep 1
done
echo "[AUTO-FLASHER] 3-minute scan timeout reached."
exit 1
