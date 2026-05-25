#pragma once
#include <cstdint>
#include <queue>
#include <unordered_set>
#include <vector>
#include <functional>
#include "shared/ecs/World.h"
#include "shared/TileMap.h"

namespace dz {

constexpr float FIRE_SPREAD_INTERVAL = 0.5f;  ///< BFS wave every 0.5 s
constexpr float FIRE_TILE_TTL        = 10.0f; ///< Tile burns for 10 s
constexpr float FIRE_DPS             = 15.0f; ///< HP/s while on burning tile

// ─────────────────────────────────────────────────────────────────────────────
// FireTile — one burning cell
// ─────────────────────────────────────────────────────────────────────────────
struct FireTile {
    int16_t tx, ty;
    float   ttl;
};

inline uint32_t fireTileKey(int16_t x, int16_t y) noexcept {
    return (static_cast<uint32_t>(static_cast<uint16_t>(x)) << 16) |
            static_cast<uint16_t>(y);
}

// ─────────────────────────────────────────────────────────────────────────────
// FireSystem — BFS tile fire propagation (server-only)
//
// Rules enforced:
//   • Only TILE_FLAMMABLE tiles ignite
//   • Barricade on ignited tile → destroyBuilding callback (immediate)
//   • Turret on ignited tile → triggerExplosion callback (oil blast)
//   • CombatComponent::isOnFire set for entities standing on fire tiles
// ─────────────────────────────────────────────────────────────────────────────
class FireSystem {
public:
    using DestroyBuildingCB = std::function<void(uint32_t entityID, bool explosion)>;

    void onDestroyBuilding(DestroyBuildingCB cb) { m_onDestroy = std::move(cb); }

    void igniteTile(int16_t tx, int16_t ty);
    void igniteAtWorld(float wx, float wy);   ///< Convenience: converts to tile

    void update(World& world, TileMap& map, float dt);

    bool isBurning(int16_t tx, int16_t ty) const noexcept {
        return m_tileSet.count(fireTileKey(tx, ty)) != 0;
    }

    const std::vector<FireTile>& tiles() const noexcept { return m_tiles; }

private:
    void spreadBFS(World& world, TileMap& map);
    void applyEntityDamage(World& world, const TileMap& map, float dt);
    void checkBuildingContact(World& world, TileMap& map, int16_t tx, int16_t ty);

    std::vector<FireTile>          m_tiles;
    std::unordered_set<uint32_t>   m_tileSet;
    std::vector<std::pair<int16_t, int16_t>> m_frontier; ///< BFS frontier
    float                          m_spreadTimer = 0.0f;
    DestroyBuildingCB              m_onDestroy;
};

} // namespace dz
