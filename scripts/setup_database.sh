#!/bin/bash
# One-step local DB setup for DeadZone.
# Creates the MySQL database, app user, schema, and a test login.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
ENV_FILE="$ROOT_DIR/.env.server"

MYSQL_HOST="${MYSQL_HOST:-127.0.0.1}"
MYSQL_PORT="${MYSQL_PORT:-3306}"
MYSQL_ADMIN_USER="${MYSQL_ADMIN_USER:-root}"
MYSQL_ADMIN_PASS="${MYSQL_ADMIN_PASS:-}"

DEADZONE_DB_HOST="${DEADZONE_DB_HOST:-127.0.0.1}"
DEADZONE_DB_USER="${DEADZONE_DB_USER:-deadzone_user}"
DEADZONE_DB_PASS="${DEADZONE_DB_PASS:-Deadzone1234!}"
DEADZONE_DB_NAME="${DEADZONE_DB_NAME:-deadzone}"

DEADZONE_TEST_USER="${DEADZONE_TEST_USER:-test}"
DEADZONE_TEST_PASS="${DEADZONE_TEST_PASS:-Test1234!}"

if [ ${#DEADZONE_TEST_PASS} -gt 15 ]; then
  echo "ERROR: DEADZONE_TEST_PASS must be 15 characters or fewer for the current auth packet."
  exit 1
fi

echo "=== DeadZone local database setup ==="
echo "App DB user: $DEADZONE_DB_USER / $DEADZONE_DB_PASS"
echo "Test login: $DEADZONE_TEST_USER / $DEADZONE_TEST_PASS"
echo ""

if ! command -v mysql >/dev/null 2>&1; then
  echo "ERROR: mysql client is not installed or not on PATH."
  echo "Install MySQL first, then rerun this script."
  echo "macOS Homebrew example: brew install mysql && brew services start mysql"
  exit 1
fi

if command -v brew >/dev/null 2>&1; then
  if brew services list 2>/dev/null | grep -E '^mysql(@[0-9.]+)?[[:space:]]+' | grep -qv started; then
    echo "MySQL appears to be installed but not running. Trying: brew services start mysql"
    brew services start mysql >/dev/null 2>&1 || true
    sleep 2
  fi
fi

if [ -z "$MYSQL_ADMIN_PASS" ] && [ -t 0 ]; then
  printf "MySQL admin password for %s (press Enter if none): " "$MYSQL_ADMIN_USER"
  IFS= read -rs MYSQL_ADMIN_PASS
  printf "\n"
fi

admin_mysql() {
  if [ -n "$MYSQL_ADMIN_PASS" ]; then
    MYSQL_PWD="$MYSQL_ADMIN_PASS" mysql \
      -h"$MYSQL_HOST" -P"$MYSQL_PORT" -u"$MYSQL_ADMIN_USER" "$@"
  else
    mysql -h"$MYSQL_HOST" -P"$MYSQL_PORT" -u"$MYSQL_ADMIN_USER" "$@"
  fi
}

app_mysql() {
  MYSQL_PWD="$DEADZONE_DB_PASS" mysql \
    -h"$DEADZONE_DB_HOST" -P"$MYSQL_PORT" -u"$DEADZONE_DB_USER" "$DEADZONE_DB_NAME" "$@"
}

echo "Creating database '$DEADZONE_DB_NAME' and MySQL user '$DEADZONE_DB_USER'..."
echo "If this fails with Access denied, rerun with MYSQL_ADMIN_PASS or enter the correct password when prompted."
admin_mysql <<SQL
CREATE DATABASE IF NOT EXISTS \`$DEADZONE_DB_NAME\`
  CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
CREATE USER IF NOT EXISTS '$DEADZONE_DB_USER'@'localhost'
  IDENTIFIED BY '$DEADZONE_DB_PASS';
CREATE USER IF NOT EXISTS '$DEADZONE_DB_USER'@'%'
  IDENTIFIED BY '$DEADZONE_DB_PASS';
ALTER USER '$DEADZONE_DB_USER'@'localhost'
  IDENTIFIED BY '$DEADZONE_DB_PASS';
ALTER USER '$DEADZONE_DB_USER'@'%'
  IDENTIFIED BY '$DEADZONE_DB_PASS';
GRANT ALL PRIVILEGES ON \`$DEADZONE_DB_NAME\`.* TO '$DEADZONE_DB_USER'@'localhost';
GRANT ALL PRIVILEGES ON \`$DEADZONE_DB_NAME\`.* TO '$DEADZONE_DB_USER'@'%';
FLUSH PRIVILEGES;
SQL

echo "Creating schema and test account..."
app_mysql <<SQL
CREATE TABLE IF NOT EXISTS accounts (
  id            BIGINT UNSIGNED  NOT NULL AUTO_INCREMENT,
  username      VARCHAR(32)      NOT NULL UNIQUE,
  password_hash VARCHAR(64)      NOT NULL COMMENT 'SHA-256 hex',
  money         INT              NOT NULL DEFAULT 1000,
  created_at    DATETIME         NOT NULL DEFAULT CURRENT_TIMESTAMP,
  last_login    DATETIME         NULL,
  PRIMARY KEY (id),
  INDEX idx_username (username)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS inventory (
  id            BIGINT UNSIGNED  NOT NULL AUTO_INCREMENT,
  username      VARCHAR(32)      NOT NULL,
  slot_index    TINYINT UNSIGNED NOT NULL COMMENT '0-based slot index',
  is_equipped   TINYINT UNSIGNED NOT NULL DEFAULT 0,
  item_id       INT              NOT NULL DEFAULT 0,
  item_key      VARCHAR(64)      NOT NULL DEFAULT '',
  category      TINYINT UNSIGNED NOT NULL DEFAULT 0,
  quantity      INT              NOT NULL DEFAULT 1,
  weight        FLOAT            NOT NULL DEFAULT 0.0,
  PRIMARY KEY (id),
  UNIQUE KEY idx_slot (username, is_equipped, slot_index),
  FOREIGN KEY (username) REFERENCES accounts(username)
    ON UPDATE CASCADE ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS player_stats (
  username      VARCHAR(32)      NOT NULL,
  kills         INT              NOT NULL DEFAULT 0,
  deaths        INT              NOT NULL DEFAULT 0,
  extractions   INT              NOT NULL DEFAULT 0,
  games_played  INT              NOT NULL DEFAULT 0,
  PRIMARY KEY (username),
  FOREIGN KEY (username) REFERENCES accounts(username)
    ON UPDATE CASCADE ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

INSERT INTO accounts (username, password_hash)
VALUES ('$DEADZONE_TEST_USER', SHA2('$DEADZONE_TEST_PASS', 256))
ON DUPLICATE KEY UPDATE password_hash = VALUES(password_hash);

INSERT IGNORE INTO player_stats (username)
VALUES ('$DEADZONE_TEST_USER');

INSERT INTO inventory (username, slot_index, is_equipped, item_id, item_key, category, quantity, weight)
VALUES
  ('$DEADZONE_TEST_USER', 0, 2, 4,  'pistol_9mm', 1, 9,  1.0),
  ('$DEADZONE_TEST_USER', 1, 2, 10, 'ammo_9mm',   2, 30, 0.3),
  ('$DEADZONE_TEST_USER', 2, 2, 30, 'medkit',     3, 1,  1.0),
  ('$DEADZONE_TEST_USER', 3, 2, 32, 'food_can',   3, 2,  0.4)
ON DUPLICATE KEY UPDATE
  item_id = VALUES(item_id),
  item_key = VALUES(item_key),
  category = VALUES(category),
  quantity = VALUES(quantity),
  weight = VALUES(weight);
SQL

cat > "$ENV_FILE" <<EOF
DEADZONE_DB_HOST=$DEADZONE_DB_HOST
DEADZONE_DB_USER=$DEADZONE_DB_USER
DEADZONE_DB_PASS=$DEADZONE_DB_PASS
DEADZONE_DB_NAME=$DEADZONE_DB_NAME
DEADZONE_TEST_USER=$DEADZONE_TEST_USER
DEADZONE_TEST_PASS=$DEADZONE_TEST_PASS
EOF

chmod 600 "$ENV_FILE"

echo "Database setup complete."
echo "Wrote $ENV_FILE"
echo "Test login: $DEADZONE_TEST_USER / $DEADZONE_TEST_PASS"
