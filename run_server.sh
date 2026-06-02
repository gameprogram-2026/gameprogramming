#!/bin/bash
# DeadZone 서버 실행 스크립트
set -a
if [ -f "$(dirname "$0")/.env.server" ]; then
  source "$(dirname "$0")/.env.server"
else
  echo "Warning: .env.server not found; starting without DB environment."
fi
set +a

exec "$(dirname "$0")/build/bin/DeadZoneServer" "$@"
