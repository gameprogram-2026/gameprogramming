#!/bin/bash
# DeadZone 서버 실행 스크립트
set -a
if [ -f "$(dirname "$0")/.env.server" ]; then
  source "$(dirname "$0")/.env.server"
else
  echo "Warning: .env.server not found; starting without DB environment."
fi
set +a

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
python3 "$ROOT_DIR/src/generate_map.py"

exec "$ROOT_DIR/build/bin/DeadZoneServer" "$@"
