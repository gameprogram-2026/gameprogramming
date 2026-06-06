#!/bin/bash
# DeadZone server launcher. Ensures local DB config exists before starting.
set -e

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
ENV_FILE="$ROOT_DIR/.env.server"

if [ ! -f "$ENV_FILE" ]; then
  echo ".env.server not found. Running local database setup..."
  "$ROOT_DIR/scripts/setup_database.sh"
fi

set -a
source "$ENV_FILE"
set +a

python3 "$ROOT_DIR/src/generate_map.py"

echo "DB: ${DEADZONE_DB_USER}@${DEADZONE_DB_HOST}/${DEADZONE_DB_NAME}"
echo "Test login: ${DEADZONE_TEST_USER:-test} / ${DEADZONE_TEST_PASS:-Test1234!}"

exec "$ROOT_DIR/build/bin/DeadZoneServer" "$@"
