#pragma once
#include <cstdint>
#include <functional>

namespace dz {

// ─────────────────────────────────────────────────────────────────────────────
// Collision layers (bitmask) — entity only collides if layers intersect
// ─────────────────────────────────────────────────────────────────────────────
enum CollisionLayer : uint16_t {
    COL_NONE        = 0,
    COL_PLAYER      = 1 << 0,
    COL_ZOMBIE      = 1 << 1,
    COL_PROJECTILE  = 1 << 2,
    COL_BUILDING    = 1 << 3,   ///< Barricades, walls
    COL_LOOT        = 1 << 4,
    COL_FIRE        = 1 << 5,
    COL_WORLD       = 1 << 6,   ///< Static map geometry

    COL_ALL         = 0xFFFF,
};

// ─────────────────────────────────────────────────────────────────────────────
// CollisionComponent — axis-aligned bounding box (AABB)
//
// Offset is relative to the entity's TransformComponent position.
// The physics/collision system runs on the server; clients predict locally
// and reconcile against server corrections.
// ─────────────────────────────────────────────────────────────────────────────
struct CollisionComponent {
    // ── AABB offset from entity origin ───────────────────────────────────────
    float offsetX = 0.0f;
    float offsetY = 0.0f;
    float width   = 16.0f;
    float height  = 16.0f;

    // ── Flags ─────────────────────────────────────────────────────────────────
    bool isSolid    = true;   ///< Blocks movement
    bool isTrigger  = false;  ///< Overlap-only (loot pickup range, etc.)
    bool isStatic   = false;  ///< Never moves (walls, world geometry)

    // ── Filtering ─────────────────────────────────────────────────────────────
    uint16_t layer     = COL_NONE;  ///< Layer this entity belongs to
    uint16_t maskLayer = COL_NONE;  ///< Layers this entity collides with

    // ── Runtime state (set by CollisionSystem) ────────────────────────────────
    bool isGrounded = false;

    // ── Helpers ───────────────────────────────────────────────────────────────
    bool canCollideWith(const CollisionComponent& other) const noexcept {
        return (layer & other.maskLayer) || (other.layer & maskLayer);
    }
};

} // namespace dz
