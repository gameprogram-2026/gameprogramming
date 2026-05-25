#include "FireSystem.h"
#include "shared/ecs/components/TransformComponent.h"
#include "shared/ecs/components/HealthComponent.h"
#include "shared/ecs/components/CombatComponent.h"
#include "shared/ecs/components/BuildingComponent.h"
#include "shared/ecs/components/NetworkComponent.h"
#include "shared/util/Logger.h"
#include <array>
#include <algorithm>

namespace dz {

static const std::array<std::pair<int,int>, 4> NEIGHBORS = {{
    {1,0},{-1,0},{0,1},{0,-1}
}};

void FireSystem::igniteTile(int16_t tx, int16_t ty) {
    uint32_t key = fireTileKey(tx, ty);
    if (m_tileSet.count(key)) return; // already burning
    m_tileSet.insert(key);
    m_tiles.push_back({tx, ty, FIRE_TILE_TTL});
    m_frontier.push_back({tx, ty});
}

void FireSystem::igniteAtWorld(float wx, float wy) {
    igniteTile(static_cast<int16_t>(TileMap::worldToTile(wx)),
               static_cast<int16_t>(TileMap::worldToTile(wy)));
}

// ─────────────────────────────────────────────────────────────────────────────
void FireSystem::update(World& world, TileMap& map, float dt) {
    // ── Decay tile TTL ────────────────────────────────────────────────────────
    for (auto& tile : m_tiles) tile.ttl -= dt;
    m_tiles.erase(
        std::remove_if(m_tiles.begin(), m_tiles.end(),
            [&](const FireTile& ft) {
                if (ft.ttl <= 0.0f) {
                    m_tileSet.erase(fireTileKey(ft.tx, ft.ty));
                    return true;
                }
                return false;
            }),
        m_tiles.end());

    // ── BFS spread every FIRE_SPREAD_INTERVAL ─────────────────────────────────
    m_spreadTimer += dt;
    if (m_spreadTimer >= FIRE_SPREAD_INTERVAL) {
        m_spreadTimer -= FIRE_SPREAD_INTERVAL;
        spreadBFS(world, map);
    }

    // ── Entity damage / isOnFire flag ─────────────────────────────────────────
    applyEntityDamage(world, map, dt);
}

// ─────────────────────────────────────────────────────────────────────────────
// spreadBFS — true BFS wave: only check neighbors of the current frontier
// ─────────────────────────────────────────────────────────────────────────────
void FireSystem::spreadBFS(World& world, TileMap& map) {
    if (m_frontier.empty()) return;

    // Snapshot current frontier to avoid modifying while iterating
    std::vector<std::pair<int16_t, int16_t>> currentFrontier = m_frontier;
    m_frontier.clear();

    for (const auto& ft : currentFrontier) {
        for (auto [dx, dy] : NEIGHBORS) {
            int16_t nx = static_cast<int16_t>(ft.first + dx);
            int16_t ny = static_cast<int16_t>(ft.second + dy);

            if (!map.inBounds(nx, ny)) continue;
            uint32_t key = fireTileKey(nx, ny);
            if (m_tileSet.count(key)) continue; // already burning

            // Only wood/debris tiles ignite
            if (!map.isFlammable(nx, ny)) {
                // Check if a building occupies this tile
                checkBuildingContact(world, map, nx, ny);
                continue;
            }

            DZ_LOG_DEBUG("[Fire] Tile (%d,%d) ignited", nx, ny);
            igniteTile(nx, ny);
            map.burnTile(nx, ny); // tile becomes ash (no longer flammable)
            checkBuildingContact(world, map, nx, ny);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// checkBuildingContact — fire hits an occupied tile → destroy/explode building
// ─────────────────────────────────────────────────────────────────────────────
void FireSystem::checkBuildingContact(World& world, TileMap& map,
                                       int16_t tx, int16_t ty) {
    if (!map.inBounds(tx, ty)) return;
    const Tile& tile = map.at(tx, ty);
    if (!tile.isOccupied() || tile.entityID == 0) return;

    Entity buildingEntity{tile.entityID};
    auto* bld = world.tryGet<BuildingComponent>(buildingEntity);
    if (!bld || bld->isDestroyed) return;

    bld->isDestroyed = true;
    map.clearOccupied(tx, ty);

    if (bld->isBarricade()) {
        DZ_LOG_INFO("[Fire] Barricade %u burned at tile (%d,%d)",
                    tile.entityID, tx, ty);
        if (m_onDestroy) m_onDestroy(tile.entityID, false);
    } else if (bld->isTurret()) {
        DZ_LOG_INFO("[Fire] Turret %u exploded at tile (%d,%d) — oil blast",
                    tile.entityID, tx, ty);
        // Ignite tiles within explosion radius to simulate oil spread
        float wx = TileMap::tileCentre(tx);
        float wy = TileMap::tileCentre(ty);
        int   blastTiles = static_cast<int>(bld->explosionRadius / TILE_SIZE);
        for (int ex = -blastTiles; ex <= blastTiles; ++ex)
            for (int ey = -blastTiles; ey <= blastTiles; ++ey)
                igniteTile(static_cast<int16_t>(tx + ex),
                            static_cast<int16_t>(ty + ey));
        if (m_onDestroy) m_onDestroy(tile.entityID, true); // explosion=true
        (void)wx; (void)wy;
    }
    world.destroyEntity(buildingEntity);
}

// ─────────────────────────────────────────────────────────────────────────────
// applyEntityDamage — set isOnFire for entities standing on burning tiles
//   (actual HP loss is handled by CombatSystem::tickFireDamage each tick)
// ─────────────────────────────────────────────────────────────────────────────
void FireSystem::applyEntityDamage(World& world, const TileMap& /*map*/, float dt) {
    (void)dt;
    auto& xfPool  = world.pool<TransformComponent>();
    auto& cbtPool = world.pool<CombatComponent>();

    for (size_t i = 0; i < xfPool.owners().size(); ++i) {
        Entity e{xfPool.owners()[i]};
        auto& xf  = xfPool.data()[i];
        auto* cbt = cbtPool.get(e.id);
        if (!cbt) continue;

        int16_t tx = static_cast<int16_t>(TileMap::worldToTile(xf.x));
        int16_t ty = static_cast<int16_t>(TileMap::worldToTile(xf.y));

        bool onFire = isBurning(tx, ty);
        if (onFire && !cbt->isOnFire) {
            cbt->isOnFire        = true;
            cbt->fireDamageTimer = 0.0f;
        } else if (!onFire) {
            cbt->isOnFire = false;
        }
    }
}

} // namespace dz
