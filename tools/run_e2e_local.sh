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
"$BUILD/udp_server" \
  --port 9000 \
  --metrics-port 9100 \
  --batch 64 \
  --max-clients 100 \
  --verbose \
  --echo &
SRV_PID=$!
echo "[E2E] server started (pid=$SRV_PID) on UDP :9000, metrics :9100, max-clients=100"
sleep 0.5
 
# Launch clients and collect their PIDs (log each launch; use --verbose)
PIDS=()
for i in $(seq 1 10); do
  echo "[E2E] launching client $i..."
  "$BUILD/udp_client" \
    --server 127.0.0.1 \
    --port 9000 \
    --pps 10000 \
    --seconds 5 \
    --payload 64 \
    --batch 64 \
    --id "$i" \
    --verbose &
  pid=$!
  PIDS+=("$pid")
  echo "[E2E] client $i started (pid=$pid)"
done
 
# Wait only for clients to finish
for p in "${PIDS[@]}"; do
  if ! wait "$p"; then
    echo "[E2E] WARNING: client process (pid=$p) exited with non-zero status"
  fi
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