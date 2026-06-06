#include "FireSystem.h"
#include "shared/ecs/components/TransformComponent.h"
#include "shared/ecs/components/HealthComponent.h"
#include "shared/ecs/components/CombatComponent.h"
#include "shared/ecs/components/BuildingComponent.h"
#include "shared/ecs/components/NetworkComponent.h"
#include "shared/util/Logger.h"
#include <array>
#include <algorithm>
#include <cmath>

namespace dz {

static const std::array<std::pair<int,int>, 4> NEIGHBORS = {{
    {1,0},{-1,0},{0,1},{0,-1}
}};

void FireSystem::igniteTile(int16_t tx, int16_t ty,
                            float ttl, float dps, bool canSpread) {
    uint32_t key = fireTileKey(tx, ty);
    if (m_tileSet.count(key)) {
        for (auto& tile : m_tiles) {
            if (tile.tx != tx || tile.ty != ty) continue;
            tile.ttl = std::max(tile.ttl, ttl);
            tile.dps = std::max(tile.dps, dps);
            if (canSpread && !tile.canSpread) {
                tile.canSpread = true;
                m_frontier.push_back({tx, ty});
            }
            break;
        }
        return;
    }
    m_tileSet.insert(key);
    m_tiles.push_back({tx, ty, ttl, dps, canSpread});
    if (canSpread) {
        m_frontier.push_back({tx, ty});
    }
}

void FireSystem::igniteAtWorld(float wx, float wy,
                               float ttl, float dps, bool canSpread) {
    igniteTile(static_cast<int16_t>(TileMap::worldToTile(wx)),
               static_cast<int16_t>(TileMap::worldToTile(wy)),
               ttl, dps, canSpread);
}

void FireSystem::reset() {
    m_tiles.clear();
    m_tileSet.clear();
    m_frontier.clear();
    m_spreadTimer = 0.0f;
}

// ─────────────────────────────────────────────────────────────────────────────
void FireSystem::update(World& world, TileMap& map, float dt) {
    for (const auto& tile : m_tiles) {
        checkBuildingContact(world, map, tile.tx, tile.ty);
    }

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

float FireSystem::tileDps(int16_t tx, int16_t ty) const noexcept {
    for (const auto& tile : m_tiles) {
        if (tile.tx == tx && tile.ty == ty) {
            return tile.dps;
        }
    }
    return 0.0f;
}

// ─────────────────────────────────────────────────────────────────────────────
// checkBuildingContact — fire hits an occupied tile → destroy/explode building
// ─────────────────────────────────────────────────────────────────────────────
void FireSystem::checkBuildingContact(World& world, TileMap& map,
                                       int16_t tx, int16_t ty) {
    if (!map.inBounds(tx, ty)) return;
    const Tile& tile = map.at(tx, ty);
    if (!tile.isOccupied() || tile.entityID == 0) return;
    const uint32_t buildingID = tile.entityID;

    Entity buildingEntity{buildingID};
    auto* bld = world.tryGet<BuildingComponent>(buildingEntity);
    if (!bld || bld->isDestroyed) return;

    bld->isDestroyed = true;
    map.clearOccupied(tx, ty);

    if (bld->isBarricade()) {
        DZ_LOG_INFO("[Fire] Barricade %u burned at tile (%d,%d)",
                    buildingID, tx, ty);
        if (m_onDestroy) m_onDestroy(buildingID, false);
    } else if (bld->isTurret()) {
        DZ_LOG_INFO("[Fire] Turret %u exploded at tile (%d,%d) — oil blast",
                    buildingID, tx, ty);
        // Ignite tiles within explosion radius to simulate oil spread
        float wx = TileMap::tileCentre(tx);
        float wy = TileMap::tileCentre(ty);
        int   blastTiles = static_cast<int>(bld->explosionRadius / TILE_SIZE);
        for (int ex = -blastTiles; ex <= blastTiles; ++ex)
            for (int ey = -blastTiles; ey <= blastTiles; ++ey)
                igniteTile(static_cast<int16_t>(tx + ex),
                            static_cast<int16_t>(ty + ey));
        if (m_onDestroy) m_onDestroy(buildingID, true); // explosion=true
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

        float fireDps = 0.0f;
        for (int ox = -1; ox <= 1; ++ox) {
            for (int oy = -1; oy <= 1; ++oy) {
                const int16_t ntx = static_cast<int16_t>(tx + ox);
                const int16_t nty = static_cast<int16_t>(ty + oy);
                if (!isBurning(ntx, nty)) continue;
                const float cx = TileMap::tileCentre(ntx);
                const float cy = TileMap::tileCentre(nty);
                const float dx = xf.x - cx;
                const float dy = xf.y - cy;
                if (dx * dx + dy * dy <= (TILE_SIZE * 0.95f) * (TILE_SIZE * 0.95f)) {
                    fireDps = std::max(fireDps, tileDps(ntx, nty));
                }
            }
        }
        if (fireDps > 0.0f) {
            if (!cbt->isOnFire) {
                cbt->fireDamageTimer = 0.0f;
            }
            cbt->isOnFire = true;
            cbt->fireDps  = fireDps;
        } else {
            cbt->isOnFire = false;
            cbt->fireDps  = 0.0f;
        }
    }
}

} // namespace dz
