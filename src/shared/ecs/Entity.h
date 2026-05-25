#pragma once
#include <cstdint>
#include <limits>

namespace dz {

// ─────────────────────────────────────────────────────────────────────────────
// Entity
//
// An Entity is nothing but a unique 32-bit handle.  All data lives in
// Components; all logic lives in Systems.
// ─────────────────────────────────────────────────────────────────────────────

using EntityID = uint32_t;

constexpr EntityID NULL_ENTITY = std::numeric_limits<EntityID>::max();

/// Thin wrapper so callers can distinguish "entity id" from raw ints.
struct Entity {
    EntityID id = NULL_ENTITY;

    bool isValid() const noexcept { return id != NULL_ENTITY; }
    bool operator==(const Entity& o) const noexcept { return id == o.id; }
    bool operator!=(const Entity& o) const noexcept { return id != o.id; }
    bool operator< (const Entity& o) const noexcept { return id <  o.id; }
};

} // namespace dz
