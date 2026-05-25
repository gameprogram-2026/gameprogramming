#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include "shared/ecs/World.h"
#include "shared/network/Protocol.h"

namespace dz {

constexpr float EXTRACTION_CHANNEL_TIME = 5.0f;    ///< Seconds to extract
constexpr float EXTRACTION_ZONE_RADIUS  = 64.0f;   ///< px — must stand inside
constexpr int   MAX_EXTRACTION_ZONES    = 4;
constexpr float EXTRACTION_OPEN_DELAY   = 300.0f;  ///< 5분 후 탈출존 활성화

// ─────────────────────────────────────────────────────────────────────────────
// ExtractionZone — one of the 4 corner zones on the map
// ─────────────────────────────────────────────────────────────────────────────
struct ExtractionZone {
    uint8_t id   = 0;
    float   cx   = 0.0f;  ///< Centre world X
    float   cy   = 0.0f;
    bool    open = true;  ///< Can become closed after use (optional)
};

// ─────────────────────────────────────────────────────────────────────────────
// PlayerExtractionState — per-player channeling progress
// ─────────────────────────────────────────────────────────────────────────────
struct PlayerExtractionState {
    bool    channeling    = false;
    float   channelTimer  = 0.0f;   ///< Seconds accumulated (reset on move/damage)
    uint8_t zoneID        = 0xFF;
    float   lastX         = 0.0f;
    float   lastY         = 0.0f;
    float   lastHp        = 0.0f;
};

// ─────────────────────────────────────────────────────────────────────────────
// ExtractionSystem — handles 5-second channeling and loot-preservation rules
//
// Extraction rules:
//   • Player must stand still (< 4 px movement) for EXTRACTION_CHANNEL_TIME.
//   • Any damage resets the timer.
//   • On success → extracted player disconnects with full inventory preserved.
//   • On death → entire inventory dropped as world loot entities.
// ─────────────────────────────────────────────────────────────────────────────
class ExtractionSystem {
public:
    using ExtractedCB = std::function<void(Entity player, uint8_t zoneID)>;
    using DeathLootCB = std::function<void(Entity player)>;
    using ExtractionOpenCB = std::function<void()>;

    void onExtracted(ExtractedCB cb) { m_onExtracted = std::move(cb); }
    void onDeathLoot(DeathLootCB cb) { m_onDeathLoot = std::move(cb); }
    void onExtractionOpen(ExtractionOpenCB cb) { m_onExtractionOpen = std::move(cb); }

    void addZone(const ExtractionZone& zone);
    void update(World& world, float dt, float gameTime);

    void reset() {
        m_zonesOpen = false;
        for (int i = 0; i < m_zoneCount; ++i) {
            m_zones[i].open = false;
        }
        for (auto& s : m_states) {
            s.entityID = 0;
            s.state = {};
        }
    }

    /// 탈출 가능한 존이 열려있는지 (5분 경과 여부)
    bool zonesOpen() const { return m_zonesOpen; }
    float openCountdown(float gameTime) const {
        float r = EXTRACTION_OPEN_DELAY - gameTime;
        return r > 0.0f ? r : 0.0f;
    }

    /// 플레이어가 상호작용(F키)을 눌렀을 때 호출됨
    bool startChanneling(uint32_t netID, float wx, float wy, float currentHp);

    const PlayerExtractionState* stateOf(uint32_t netID) const;
    float channelProgress(uint32_t netID) const; ///< [0,1]

private:
    std::array<ExtractionZone, MAX_EXTRACTION_ZONES> m_zones{};
    int  m_zoneCount = 0;
    bool m_zonesOpen = false;  ///< 5분 후 true로 전환

    // Indexed by entity id (simple map for up to 8 players)
    struct Entry { uint32_t entityID; PlayerExtractionState state; };
    std::array<Entry, MAX_CLIENTS> m_states{};

    PlayerExtractionState& stateFor(uint32_t entityID);
    int8_t findZone(float wx, float wy) const noexcept;

    ExtractedCB m_onExtracted;
    DeathLootCB m_onDeathLoot;
    ExtractionOpenCB m_onExtractionOpen;
};

} // namespace dz
