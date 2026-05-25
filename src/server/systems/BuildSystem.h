#pragma once
#include "shared/ecs/World.h"
#include "shared/TileMap.h"
#include "shared/ecs/components/BuildingComponent.h"
#include <functional>

namespace dz {

// ─────────────────────────────────────────────────────────────────────────────
// BuildSystem — validates and executes barricade/turret placement (server-only)
// ─────────────────────────────────────────────────────────────────────────────
class BuildSystem {
public:
    using SpawnCallback        = std::function<void(Entity building)>;
    using TurretFireCallback   = std::function<void(
        uint16_t turretNetID,
        float fromX, float fromY,
        float toX,   float toY,
        uint8_t ownerTeam)>;

    void onBuildingSpawned(SpawnCallback cb)    { m_onSpawn      = std::move(cb); }
    void onTurretFire(TurretFireCallback cb)    { m_onTurretFire = std::move(cb); }

    /// Called when a player sends C2S_BuildRequest.
    /// Validates range, material, tile availability; spawns entity on success.
    /// Returns the new entity (invalid on failure).
    Entity tryBuild(World& world, TileMap& map,
                    uint32_t ownerTeam,
                    Entity   requester,
                    int      tileX, int tileY,
                    BuildingType type);

    /// Instantly destroy a building (called by FireSystem or combat).
    void destroyBuilding(World& world, TileMap& map, Entity building);

    /// Update turret auto-fire each server tick.
    void updateTurrets(World& world, float dt);

private:
    uint32_t          m_nextNetID = 5000; ///< Building netIDs start at 5000
    SpawnCallback     m_onSpawn;
    TurretFireCallback m_onTurretFire;
};

} // namespace dz
