#pragma once
#include "shared/ecs/World.h"
#include "shared/network/Packet.h"
#include "shared/TileMap.h"
#include "server/systems/BuildSystem.h"
#include "server/systems/CombatSystem.h"
#include "server/FireSystem.h"

namespace dz {

// ─────────────────────────────────────────────────────────────────────────────
// GameLogic — validates and dispatches C2S input actions on the server
// ─────────────────────────────────────────────────────────────────────────────
class GameLogic {
public:
    GameLogic(World& world, TileMap& map,
              BuildSystem& build, CombatSystem& combat, FireSystem& fire)
        : m_world(world), m_map(map), m_build(build),
          m_combat(combat), m_fire(fire) {}

    using RangedFireCallback = std::function<void(uint16_t shooterID, float fromX, float fromY, float toX, float toY, uint8_t team)>;
    void onRangedFire(RangedFireCallback cb) { m_onRangedFire = std::move(cb); }

    /// Main dispatcher — called per-tick for each connected client's input
    void processInput(uint32_t ownerID, const InputPacket& pkt);

    bool handleBuildRequest(uint32_t ownerID,
                            int tileX, int tileY, BuildingType type,
                            uint8_t direction = 0);
    void handleFireThrow(uint32_t ownerID,
                         float originX, float originY);
    void handleFlamethrowerBurst(uint32_t ownerID, float aimAngle);
    void handleMeleeAttack(uint32_t ownerID);
    void handleRangedFire(uint32_t ownerID, float aimAngle);
    void handleReload(uint32_t ownerID);
    void handleUseItem(uint32_t ownerID, const char* key);
    bool handleCraftRequest(uint32_t ownerID, uint8_t recipeID);
    void handleLootPickup(uint32_t ownerID, uint32_t lootNetID);

private:
    Entity findOwnedEntity(uint32_t ownerID);

    World&          m_world;
    TileMap&        m_map;
    BuildSystem&    m_build;
    CombatSystem&   m_combat;
    FireSystem&     m_fire;

    RangedFireCallback m_onRangedFire;
};

} // namespace dz
