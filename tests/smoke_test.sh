#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PORT="${PORT:-8899}"
LOG_DIR="$ROOT_DIR/tests/tmp"
SERVER_LOG="$LOG_DIR/server.log"
SERVER_PID=""

cleanup() {
  if [[ -n "$SERVER_PID" ]]; then
    kill "$SERVER_PID" >/dev/null 2>&1 || true
    wait "$SERVER_PID" >/dev/null 2>&1 || true
  fi
}

mkdir -p "$LOG_DIR"
trap cleanup EXIT

cd "$ROOT_DIR"
rm -f my.db
make clean
make

./dict_server 127.0.0.1 "$PORT" >"$SERVER_LOG" 2>&1 &
SERVER_PID="$!"

sleep 1
python3 tests/smoke_client.py 127.0.0.1 "$PORT"

echo "Smoke test passed."
