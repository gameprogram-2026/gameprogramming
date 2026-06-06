#!/bin/bash
# Double-click launcher for macOS Finder.
set -e

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT_DIR"

echo "=== DeadZone one-click launcher ==="

if [ ! -x "$ROOT_DIR/build/bin/DeadZoneServer" ] || [ ! -x "$ROOT_DIR/build/bin/DeadZoneClient" ]; then
  echo "Build output not found. Configuring and building DeadZone..."
  cmake -S "$ROOT_DIR" -B "$ROOT_DIR/build"
  cmake --build "$ROOT_DIR/build"
fi

if [ ! -f "$ROOT_DIR/.env.server" ]; then
  echo "First run: setting up local MySQL database and test account."
  echo "If MySQL asks for an admin password, enter the local MySQL root password."
  "$ROOT_DIR/scripts/setup_database.sh"
fi

"$ROOT_DIR/run_game.sh"

echo ""
echo "DeadZone launcher finished. You can close this window."
