#pragma once
#include "shared/ecs/World.h"
#include "shared/TileMap.h"
#include "shared/network/Packet.h"

namespace dz {

// ─────────────────────────────────────────────────────────────────────────────
// MovementSystem — server-authoritative movement + collision resolution
//
// Each tick:
//   1. Apply most recent InputPacket per player entity
//   2. Integrate velocity
//   3. Resolve AABB collision vs TileMap solid tiles
//   4. Mark TransformComponent dirty for next snapshot
// ─────────────────────────────────────────────────────────────────────────────
class MovementSystem {
public:
    // Walk / sprint speeds in pixels per second
    static constexpr float SPEED_WALK   = 128.0f;  // ~4 tiles/s
    static constexpr float SPEED_SPRINT = 224.0f;  // ~7 tiles/s
    static constexpr float SPEED_CROUCH =  64.0f;  // ~2 tiles/s
    static constexpr float ENTITY_HW    =  10.0f;  // half-width of collision box
    static constexpr float ENTITY_HH    =  10.0f;  // half-height

    void update(World& world, const TileMap& map, float dt);

    /// Apply a single input packet to the entity owned by ownerID.
    void applyInput(World& world, const TileMap& map,
                    uint32_t ownerID, const InputPacket& input, float dt);
};

} // namespace dz
