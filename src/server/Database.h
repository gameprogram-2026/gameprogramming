#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include "shared/ecs/components/InventoryComponent.h"

// Forward-declare to avoid pulling mysql.h into every TU
struct MYSQL;

namespace dz {

// ─────────────────────────────────────────────────────────────────────────────
// Database
//
// Server-side persistent store backed by MySQL.
//
// Schema overview:
//   accounts    — one row per player (uuid, username, password_hash, money, …)
//   player_stats— K/D, games played, total extractions, etc.
//   inventory   — one row per item slot per player (player_uuid, slot, key, qty…)
//
// Thread-safety:
//   GameServer calls all DB methods on the server logic thread.
//   If you add a background thread later, wrap each call with m_mutex.
// ─────────────────────────────────────────────────────────────────────────────
class Database {
public:
    Database();
    ~Database();

    // ── Lifecycle ──────────────────────────────────────────────────────────
    /// Connect to MySQL and create tables if they don't exist.
    /// @param host     e.g. "127.0.0.1"
    /// @param user     e.g. "deadzone_user"
    /// @param password e.g. "s3cr3t"
    /// @param dbName   e.g. "deadzone"
    /// @param port     typically 3306
    bool init(const std::string& host,
              const std::string& user,
              const std::string& password,
              const std::string& dbName,
              uint16_t           port = 3306);

    void shutdown();

    // ── Account management ─────────────────────────────────────────────────
    /// Returns false if the username already exists.
    bool registerAccount(const std::string& username,
                         const std::string& password);

    /// Returns false if credentials are wrong or account doesn't exist.
    /// On success, outInv is populated from the DB.
    bool loginAccount(const std::string& username,
                      const std::string& password,
                      InventoryComponent& outInv);

    // ── In-game persistence ────────────────────────────────────────────────
    /// Save/replace the player's full inventory and money into the DB.
    /// Called on successful extraction or on server shutdown.
    void saveAccount(const std::string& username,
                     const InventoryComponent& inv);

    // ── Player stats ───────────────────────────────────────────────────────
    struct PlayerStats {
        int kills       = 0;
        int deaths      = 0;
        int extractions = 0;
        int gamesPlayed = 0;
    };

    void recordKill      (const std::string& username);
    void recordDeath     (const std::string& username);
    void recordExtraction(const std::string& username);
    PlayerStats getStats (const std::string& username);

private:
    MYSQL*      m_conn    = nullptr;
    std::string m_dbName;

    // ── Helpers ────────────────────────────────────────────────────────────
    bool        createTables();
    bool        query(const std::string& sql);
    bool        queryf(const char* fmt, ...);   ///< printf-style convenience

    /// Hash a password using SHA-256 via MySQL's own SHA2() function.
    /// Returns the 64-char hex string, or "" on failure.
    std::string hashPassword(const std::string& plain);
};

} // namespace dz
