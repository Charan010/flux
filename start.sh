#!/bin/bash

echo "[*] Starting flux daemon..."

flux & FLUX_PID=$!

echo "[*] flux daemon pid: $FLUX_PID"

cleanup() {
    echo
    echo "[*] Stopping flux daemon..."
    kill "$FLUX_PID" 2>/dev/null || true
}

trap cleanup EXIT INT TERM

while [ ! -S /tmp/flux.sock ]; do
    sleep 0.05
done

echo "[*] Starting Go server..."

cd server
go run main.go