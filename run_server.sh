#!/bin/bash
# DeadZone 서버 실행 스크립트
set -a
source "$(dirname "$0")/.env.server"
set +a

exec "$(dirname "$0")/build/bin/DeadZoneServer" "$@"
