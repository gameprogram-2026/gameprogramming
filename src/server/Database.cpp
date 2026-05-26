// ──────────────────────────────────────────────────────────────────────────────
// Database.cpp — MySQL-backed persistent store for DeadZone
// ──────────────────────────────────────────────────────────────────────────────
#include "Database.h"
#include "shared/util/Logger.h"

#include <mysql.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <sstream>

namespace dz {

// ─────────────────────────────────────────────────────────────────────────────
// Construction / Destruction
// ─────────────────────────────────────────────────────────────────────────────
Database::Database()  { mysql_library_init(0, nullptr, nullptr); }
Database::~Database() { shutdown(); mysql_library_end(); }

// ─────────────────────────────────────────────────────────────────────────────
// init — connect and create schema
// ─────────────────────────────────────────────────────────────────────────────
bool Database::init(const std::string& host,
                    const std::string& user,
                    const std::string& password,
                    const std::string& dbName,
                    uint16_t           port)
{
    m_dbName = dbName;

    m_conn = mysql_init(nullptr);
    if (!m_conn) {
        DZ_LOG_FATAL("[DB] mysql_init() failed — out of memory");
        return false;
    }

    // Set charset to utf8mb4 (handles emoji, etc.)
    mysql_options(m_conn, MYSQL_SET_CHARSET_NAME, "utf8mb4");

    unsigned int timeout = 10;
    mysql_options(m_conn, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);

    if (!mysql_real_connect(m_conn,
                            host.c_str(),
                            user.c_str(),
                            password.c_str(),
                            nullptr,    // connect without selecting DB first
                            port,
                            nullptr,
                            0))
    {
        DZ_LOG_FATAL("[DB] mysql_real_connect() failed: %s", mysql_error(m_conn));
        mysql_close(m_conn);
        m_conn = nullptr;
        return false;
    }

    // Create & select the database
    std::string createDB = "CREATE DATABASE IF NOT EXISTS `" + dbName
                         + "` CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci";
    if (mysql_query(m_conn, createDB.c_str()) != 0) {
        DZ_LOG_FATAL("[DB] Failed to create database: %s", mysql_error(m_conn));
        return false;
    }
    if (mysql_select_db(m_conn, dbName.c_str()) != 0) {
        DZ_LOG_FATAL("[DB] Failed to select database: %s", mysql_error(m_conn));
        return false;
    }

    if (!createTables()) return false;

    DZ_LOG_INFO("[DB] Connected to MySQL %s@%s:%u/%s",
                user.c_str(), host.c_str(), port, dbName.c_str());
    return true;
}

void Database::shutdown() {
    if (m_conn) {
        mysql_close(m_conn);
        m_conn = nullptr;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// createTables — idempotent DDL
// ─────────────────────────────────────────────────────────────────────────────
bool Database::createTables() {
    // accounts
    if (!query(R"SQL(
        CREATE TABLE IF NOT EXISTS accounts (
            id            BIGINT UNSIGNED  NOT NULL AUTO_INCREMENT,
            username      VARCHAR(32)      NOT NULL UNIQUE,
            password_hash VARCHAR(64)      NOT NULL COMMENT 'SHA-256 hex',
            money         INT              NOT NULL DEFAULT 1000,
            created_at    DATETIME         NOT NULL DEFAULT CURRENT_TIMESTAMP,
            last_login    DATETIME         NULL,
            PRIMARY KEY (id),
            INDEX idx_username (username)
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4
    )SQL")) return false;

    // inventory — grid slots
    if (!query(R"SQL(
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
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4
    )SQL")) return false;

    // player_stats
    if (!query(R"SQL(
        CREATE TABLE IF NOT EXISTS player_stats (
            username      VARCHAR(32)      NOT NULL,
            kills         INT              NOT NULL DEFAULT 0,
            deaths        INT              NOT NULL DEFAULT 0,
            extractions   INT              NOT NULL DEFAULT 0,
            games_played  INT              NOT NULL DEFAULT 0,
            PRIMARY KEY (username),
            FOREIGN KEY (username) REFERENCES accounts(username)
                ON UPDATE CASCADE ON DELETE CASCADE
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4
    )SQL")) return false;

    DZ_LOG_INFO("[DB] Schema up-to-date");
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// hashPassword — delegate to MySQL SHA2() so the hash stays on the server
// ─────────────────────────────────────────────────────────────────────────────
std::string Database::hashPassword(const std::string& plain) {
    // Escape the plain-text password before embedding it
    std::vector<char> esc(plain.size() * 2 + 1);
    mysql_real_escape_string(m_conn, esc.data(), plain.c_str(),
                             static_cast<unsigned long>(plain.size()));

    std::string sql = std::string("SELECT SHA2('") + esc.data() + "', 256)";
    if (mysql_query(m_conn, sql.c_str()) != 0) return "";

    MYSQL_RES* res = mysql_store_result(m_conn);
    if (!res) return "";

    std::string hash;
    MYSQL_ROW row = mysql_fetch_row(res);
    if (row && row[0]) hash = row[0];
    mysql_free_result(res);
    return hash;
}

// ─────────────────────────────────────────────────────────────────────────────
// registerAccount
// ─────────────────────────────────────────────────────────────────────────────
bool Database::registerAccount(const std::string& username,
                               const std::string& password)
{
    if (!m_conn) {
        DZ_LOG_WARN("[DB] registerAccount bypassed because DB is not connected.");
        return true;
    }

    std::string hash = hashPassword(password);
    if (hash.empty()) return false;

    // Escape username
    std::vector<char> escUser(username.size() * 2 + 1);
    mysql_real_escape_string(m_conn, escUser.data(), username.c_str(),
                             static_cast<unsigned long>(username.size()));

    // INSERT IGNORE — returns 0 rows if username already exists
    std::string sql =
        std::string("INSERT IGNORE INTO accounts (username, password_hash) VALUES ('")
        + escUser.data() + "', '" + hash + "')";

    if (mysql_query(m_conn, sql.c_str()) != 0) {
        DZ_LOG_ERROR("[DB] registerAccount: %s", mysql_error(m_conn));
        return false;
    }

    uint64_t affected = mysql_affected_rows(m_conn);
    if (affected == 0) {
        DZ_LOG_DEBUG("[DB] Username '%s' already exists", username.c_str());
        return false; // duplicate
    }

    // Create empty stats row
    std::string sqlStats =
        std::string("INSERT IGNORE INTO player_stats (username) VALUES ('")
        + escUser.data() + "')";
    query(sqlStats);

    // Give basic starter items in Stash
    // is_equipped = 2 means Stash
    // pistol_9mm: id=4, cat=1
    // ammo_9mm: id=10, cat=4
    // medkit: id=30, cat=5
    // food_can: id=32, cat=5
    std::string sqlStarterItems =
        std::string("INSERT IGNORE INTO inventory (username, slot_index, is_equipped, item_id, item_key, category, quantity, weight) VALUES ") +
        "('" + escUser.data() + "', 0, 2, 4, 'pistol_9mm', 1, 1, 1.0), " +
        "('" + escUser.data() + "', 1, 2, 10, 'ammo_9mm', 4, 30, 0.3), " +
        "('" + escUser.data() + "', 2, 2, 30, 'medkit', 5, 1, 1.0), " +
        "('" + escUser.data() + "', 3, 2, 32, 'food_can', 5, 2, 0.4)";
    query(sqlStarterItems);

    DZ_LOG_INFO("[DB] Account created with starter items: %s", username.c_str());
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// loginAccount
// ─────────────────────────────────────────────────────────────────────────────
bool Database::loginAccount(const std::string& username,
                            const std::string& password,
                            InventoryComponent& outInv)
{
    if (!m_conn) {
        DZ_LOG_WARN("[DB] loginAccount bypassed because DB is not connected.");
        return true;
    }

    std::string hash = hashPassword(password);
    if (hash.empty()) return false;

    std::vector<char> escUser(username.size() * 2 + 1);
    mysql_real_escape_string(m_conn, escUser.data(), username.c_str(),
                             static_cast<unsigned long>(username.size()));

    // Verify credentials
    std::string sql =
        std::string("SELECT money FROM accounts WHERE username='")
        + escUser.data() + "' AND password_hash='" + hash + "' LIMIT 1";

    if (mysql_query(m_conn, sql.c_str()) != 0) {
        DZ_LOG_ERROR("[DB] loginAccount query: %s", mysql_error(m_conn));
        return false;
    }

    MYSQL_RES* res = mysql_store_result(m_conn);
    if (!res) return false;

    MYSQL_ROW row = mysql_fetch_row(res);
    if (!row) {
        mysql_free_result(res);
        DZ_LOG_DEBUG("[DB] Login failed for '%s'", username.c_str());
        return false;
    }
    outInv.money = row[0] ? std::stoi(row[0]) : 0;
    mysql_free_result(res);

    // Update last_login
    std::string sqlUpdate =
        std::string("UPDATE accounts SET last_login=NOW() WHERE username='")
        + escUser.data() + "'";
    query(sqlUpdate);

    // Load inventory grid slots
    std::string sqlInv =
        std::string("SELECT slot_index, is_equipped, item_id, item_key, "
                    "category, quantity, weight ")
        + "FROM inventory WHERE username='" + escUser.data()
        + "' ORDER BY is_equipped, slot_index";

    if (mysql_query(m_conn, sqlInv.c_str()) != 0) {
        DZ_LOG_ERROR("[DB] load inventory: %s", mysql_error(m_conn));
        return true; // account valid but no items — still OK
    }

    MYSQL_RES* invRes = mysql_store_result(m_conn);
    if (!invRes) return true;

    outInv.slots.fill({});
    outInv.equipped.fill({});
    outInv.usedSlots      = 0;
    outInv.currentWeight  = 0.0f;

    MYSQL_ROW irow;
    while ((irow = mysql_fetch_row(invRes)) != nullptr) {
        int  slotIdx  = irow[0] ? std::stoi(irow[0]) : 0;
        int  locType  = irow[1] ? std::stoi(irow[1]) : 0;

        Item item;
        item.itemID   = irow[2] ? std::stoi(irow[2]) : 0;
        item.key      = irow[3] ? irow[3] : "";
        item.category = irow[4] ? static_cast<ItemCategory>(std::stoi(irow[4])) : ItemCategory::None;
        item.quantity = irow[5] ? std::stoi(irow[5]) : 1;
        item.weight   = irow[6] ? std::stof(irow[6]) : 0.0f;

        if (locType == 1) {
            if (slotIdx >= 0 && slotIdx < EQUIPMENT_SLOT_COUNT)
                outInv.equipped[slotIdx] = item;
        } else if (locType == 2) {
            if (slotIdx >= 0 && slotIdx < 40)
                outInv.stash[slotIdx] = item;
        } else {
            if (slotIdx >= 0 && slotIdx < INVENTORY_GRID_SLOTS) {
                outInv.slots[slotIdx] = item;
                if (item.isValid()) {
                    ++outInv.usedSlots;
                    outInv.currentWeight += item.weight * item.quantity;
                }
            }
        }
    }
    mysql_free_result(invRes);

    DZ_LOG_INFO("[DB] Login OK: %s (money=%d, slots=%d)",
                username.c_str(), outInv.money, outInv.usedSlots);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// saveAccount — transactional upsert
// ─────────────────────────────────────────────────────────────────────────────
void Database::saveAccount(const std::string& username,
                           const InventoryComponent& inv)
{
    if (!m_conn) return;

    std::vector<char> escUser(username.size() * 2 + 1);
    mysql_real_escape_string(m_conn, escUser.data(), username.c_str(),
                             static_cast<unsigned long>(username.size()));

    struct Transaction {
        MYSQL* conn;
        bool committed = false;
        Transaction(MYSQL* c) : conn(c) { mysql_query(conn, "START TRANSACTION"); }
        ~Transaction() { 
            if (!committed) {
                mysql_query(conn, "ROLLBACK");
            }
        }
        void commit() { mysql_query(conn, "COMMIT"); committed = true; }
    };
    Transaction txn(m_conn);

    // Update money
    char moneyBuf[256];
    std::snprintf(moneyBuf, sizeof(moneyBuf),
                  "UPDATE accounts SET money=%d WHERE username='%s'",
                  inv.money, escUser.data());
    if (!query(std::string(moneyBuf))) return;

    // Wipe existing inventory rows for this player
    {
        std::string delSql = std::string("DELETE FROM inventory WHERE username='")
                           + escUser.data() + "'";
        if (!query(delSql)) return;
    }

    // Insert grid slots
    for (int i = 0; i < INVENTORY_GRID_SLOTS; ++i) {
        const Item& item = inv.slots[i];
        if (!item.isValid()) continue;

        std::vector<char> escKey(item.key.size() * 2 + 1);
        mysql_real_escape_string(m_conn, escKey.data(), item.key.c_str(),
                                 static_cast<unsigned long>(item.key.size()));

        char buf[512];
        std::snprintf(buf, sizeof(buf),
            "INSERT INTO inventory (username, slot_index, is_equipped, "
            "item_id, item_key, category, quantity, weight) "
            "VALUES ('%s', %d, 0, %d, '%s', %d, %d, %f)",
            escUser.data(), i, item.itemID, escKey.data(),
            static_cast<int>(item.category), item.quantity, item.weight);

        if (!query(std::string(buf))) return;
    }

    // Insert equipped slots
    for (int i = 0; i < EQUIPMENT_SLOT_COUNT; ++i) {
        const Item& item = inv.equipped[i];
        if (!item.isValid()) continue;

        std::vector<char> escKey(item.key.size() * 2 + 1);
        mysql_real_escape_string(m_conn, escKey.data(), item.key.c_str(),
                                 static_cast<unsigned long>(item.key.size()));

        char buf[512];
        std::snprintf(buf, sizeof(buf),
            "INSERT INTO inventory (username, slot_index, is_equipped, "
            "item_id, item_key, category, quantity, weight) "
            "VALUES ('%s', %d, 1, %d, '%s', %d, %d, %f)",
            escUser.data(), i, item.itemID, escKey.data(),
            static_cast<int>(item.category), item.quantity, item.weight);

        if (!query(std::string(buf))) return;
    }

    // Insert stash slots
    for (int i = 0; i < 40; ++i) {
        const Item& item = inv.stash[i];
        if (!item.isValid()) continue;

        std::vector<char> escKey(item.key.size() * 2 + 1);
        mysql_real_escape_string(m_conn, escKey.data(), item.key.c_str(),
                                 static_cast<unsigned long>(item.key.size()));

        char buf[512];
        std::snprintf(buf, sizeof(buf),
            "INSERT INTO inventory (username, slot_index, is_equipped, "
            "item_id, item_key, category, quantity, weight) "
            "VALUES ('%s', %d, 2, %d, '%s', %d, %d, %f)",
            escUser.data(), i, item.itemID, escKey.data(),
            static_cast<int>(item.category), item.quantity, item.weight);

        if (!query(std::string(buf))) return;
    }

    txn.commit();
    DZ_LOG_INFO("[DB] Saved inventory for '%s'", username.c_str());
}

// ─────────────────────────────────────────────────────────────────────────────
// Player stats helpers
// ─────────────────────────────────────────────────────────────────────────────
void Database::recordKill(const std::string& username) {
    if (!m_conn) return;
    std::vector<char> escUser(username.size() * 2 + 1);
    mysql_real_escape_string(m_conn, escUser.data(), username.c_str(),
                             static_cast<unsigned long>(username.size()));
    char buf[256];
    std::snprintf(buf, sizeof(buf),
        "UPDATE player_stats SET kills=kills+1 WHERE username='%s'",
        escUser.data());
    query(std::string(buf));
}

void Database::recordDeath(const std::string& username) {
    if (!m_conn) return;
    std::vector<char> escUser(username.size() * 2 + 1);
    mysql_real_escape_string(m_conn, escUser.data(), username.c_str(),
                             static_cast<unsigned long>(username.size()));
    char buf[256];
    std::snprintf(buf, sizeof(buf),
        "UPDATE player_stats SET deaths=deaths+1, games_played=games_played+1 "
        "WHERE username='%s'",
        escUser.data());
    query(std::string(buf));
}

void Database::recordExtraction(const std::string& username) {
    if (!m_conn) return;
    std::vector<char> escUser(username.size() * 2 + 1);
    mysql_real_escape_string(m_conn, escUser.data(), username.c_str(),
                             static_cast<unsigned long>(username.size()));
    char buf[256];
    std::snprintf(buf, sizeof(buf),
        "UPDATE player_stats SET extractions=extractions+1, "
        "games_played=games_played+1 WHERE username='%s'",
        escUser.data());
    query(std::string(buf));
}

Database::PlayerStats Database::getStats(const std::string& username) {
    PlayerStats st;
    if (!m_conn) return st;

    std::vector<char> escUser(username.size() * 2 + 1);
    mysql_real_escape_string(m_conn, escUser.data(), username.c_str(),
                             static_cast<unsigned long>(username.size()));

    std::string sql =
        std::string("SELECT kills, deaths, extractions, games_played "
                    "FROM player_stats WHERE username='")
        + escUser.data() + "'";

    if (mysql_query(m_conn, sql.c_str()) != 0) return st;
    MYSQL_RES* res = mysql_store_result(m_conn);
    if (!res) return st;

    MYSQL_ROW row = mysql_fetch_row(res);
    if (row) {
        st.kills       = row[0] ? std::stoi(row[0]) : 0;
        st.deaths      = row[1] ? std::stoi(row[1]) : 0;
        st.extractions = row[2] ? std::stoi(row[2]) : 0;
        st.gamesPlayed = row[3] ? std::stoi(row[3]) : 0;
    }
    mysql_free_result(res);
    return st;
}

// ─────────────────────────────────────────────────────────────────────────────
// query helpers
// ─────────────────────────────────────────────────────────────────────────────
bool Database::query(const std::string& sql) {
    if (!m_conn) return false;
    if (mysql_query(m_conn, sql.c_str()) != 0) {
        DZ_LOG_ERROR("[DB] Query failed: %s | SQL: %.120s",
                     mysql_error(m_conn), sql.c_str());
        return false;
    }
    return true;
}

} // namespace dz
