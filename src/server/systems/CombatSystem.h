#pragma once
#include "shared/ecs/World.h"
#include "shared/ecs/components/HealthComponent.h"
#include <functional>
#include <cstdint>

namespace dz {

// ─────────────────────────────────────────────────────────────────────────────
// DamageResult — returned by processAttack so callers can broadcast events
// ─────────────────────────────────────────────────────────────────────────────
struct DamageResult {
    uint32_t victimID   = 0;
    uint32_t attackerID = 0;
    float    damage     = 0.0f;
    float    remainingHp= 0.0f;
    DamageType type     = DamageType::Melee;
    bool     killed     = false;
    bool     betrayal   = false; ///< Attacker and victim were allied
};

// ─────────────────────────────────────────────────────────────────────────────
// CombatSystem — server-only
//
//  • Processes melee attack requests (AABB sweep)
//  • Applies bleeding DoT each tick
//  • Handles death (loot drop signal, entity removal)
//  • Enforces friendly-fire OFF within teams
//  • Detects betrayal (attacking an ally → breaks alliance)
// ─────────────────────────────────────────────────────────────────────────────
class CombatSystem {
public:
    using DamageCallback = std::function<void(const DamageResult&)>;
    using DeathCallback  = std::function<void(Entity victim, Entity killer)>;

    void onDamage(DamageCallback cb) { m_onDamage = std::move(cb); }
    void onDeath(DeathCallback cb)   { m_onDeath  = std::move(cb); }

    void update(World& world, float dt);

    /// Try a melee swing from attacker toward its aim angle.
    /// Returns true if at least one target was hit.
    bool tryMeleeAttack(World& world, Entity attacker);

    /// Apply direct damage to an entity (used by fire, explosions, etc.)
    DamageResult applyDamage(World& world, Entity victim, Entity attacker,
                              float rawDamage, DamageType type,
                              const uint8_t* allianceMatrix = nullptr);

    /// Trigger turret explosion at world position.
    void triggerExplosion(World& world, float wx, float wy,
                          float radius, float damage,
                          Entity source);

private:
    void tickBleeding(World& world, float dt);
    void tickFireDamage(World& world, float dt);
    void tickStamina(World& world, float dt);

    DamageCallback m_onDamage;
    DeathCallback  m_onDeath;
};

} // namespace dz
