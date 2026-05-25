#pragma once
#include <cstdint>

namespace dz {

// ─────────────────────────────────────────────────────────────────────────────
// BuildingType
// ─────────────────────────────────────────────────────────────────────────────
enum class BuildingType : uint8_t {
    Barricade = 0,   ///< Wood — solid, flammable, burns immediately on fire contact
    Turret    = 1,   ///< Metal — solid, fire contact → oil explosion
    Workbench = 2,   ///< Crafting station
};

// ─────────────────────────────────────────────────────────────────────────────
// BuildingComponent — server authority, replicated to clients
// ─────────────────────────────────────────────────────────────────────────────
struct BuildingComponent {
    BuildingType type        = BuildingType::Barricade;
    uint8_t      ownerTeam   = 0;    ///< Team that placed this building
    int          tileX       = 0;    ///< Occupied tile coordinate
    int          tileY       = 0;
    float        maxHp       = 150.0f;
    float        currentHp   = 150.0f;
    bool         isDestroyed = false;

    // ── Turret-specific ───────────────────────────────────────────────────────
    float  turretFireRate     = 2.0f;   ///< Shots per second
    float  turretRange        = 320.0f; ///< Detection + fire radius (px)
    float  turretDamage       = 20.0f;
    float  turretCooldown     = 0.0f;
    uint32_t turretTargetID   = 0;

    // ── Explosion (turret on fire) ────────────────────────────────────────────
    float  explosionRadius    = 192.0f; // px = 6 tiles
    float  explosionDamage    = 80.0f;

    bool isBarricade() const noexcept { return type == BuildingType::Barricade; }
    bool isTurret()    const noexcept { return type == BuildingType::Turret; }
    bool isWorkbench() const noexcept { return type == BuildingType::Workbench; }
};

} // namespace dz
