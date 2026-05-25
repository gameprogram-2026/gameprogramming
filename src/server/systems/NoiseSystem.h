#pragma once
#include "shared/ecs/World.h"
#include <vector>
#include <cstdint>

namespace dz {

// ─────────────────────────────────────────────────────────────────────────────
// WorldNoiseEvent — global noise entry visible to zombie AI
// ─────────────────────────────────────────────────────────────────────────────
struct WorldNoiseEvent {
    float    x, y;          ///< World position of source
    float    radius;         ///< Detection radius
    uint8_t  category;       ///< NoiseCategory value
    float    ttl;            ///< Seconds until expiry
};

// ─────────────────────────────────────────────────────────────────────────────
// NoiseSystem — collects per-entity noise each tick into a global list
//
// ZombieAISystem queries this list to determine state transitions.
// The list is rebuilt every tick (CombatComponent::emitNoise sets flags,
// this system harvests them).
// ─────────────────────────────────────────────────────────────────────────────
class NoiseSystem {
public:
    void update(World& world, float dt);

    const std::vector<WorldNoiseEvent>& events() const noexcept { return m_events; }

    /// Add a one-off noise event not tied to an entity (e.g. explosion).
    void addEvent(float x, float y, float radius, uint8_t category, float ttl = 1.0f);

    /// Returns the loudest noise category within `radius` pixels of (cx, cy).
    uint8_t maxCategoryNear(float cx, float cy, float radius) const noexcept;

private:
    std::vector<WorldNoiseEvent> m_events;
};

} // namespace dz
