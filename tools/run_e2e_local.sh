#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build-e2e"
rm -rf "$BUILD"
mkdir -p "$BUILD"
pushd "$BUILD" >/dev/null
cmake -DCMAKE_BUILD_TYPE=Release "$ROOT"
make -j
popd >/dev/null
 
# Start server in background and capture its PID
"$BUILD/udp_server" --port 9000 --metrics-port 9100 --batch 64 --verbose --echo &
SRV_PID=$!
sleep 0.5
 
# Launch clients and collect their PIDs
PIDS=()
for i in $(seq 1 10); do
  "$BUILD/udp_client" --server 127.0.0.1 --port 9000 --pps 10000 --seconds 5 --payload 64 --batch 64 --id $i &
  PIDS+=($!)
done
 
# Wait only for clients to finish
for p in "${PIDS[@]}"; do
  wait "$p"
done
 
# Give server a moment to print final stats, then terminate it
sleep 1
kill -TERM "$SRV_PID" 2>/dev/null || true
 
# Wait for graceful shutdown; force kill if needed after timeout
for _ in $(seq 1 5); do
  if ! kill -0 "$SRV_PID" 2>/dev/null; then
    break
  fi
  sleep 1
done
if kill -0 "$SRV_PID" 2>/dev/null; then
  kill -KILL "$SRV_PID" 2>/dev/null || true
fi
 
echo "[E2E] Completed. Check server output above for sustained rate >= 100 kpps."
 
 