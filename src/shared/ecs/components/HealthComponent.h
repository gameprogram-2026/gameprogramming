#pragma once
#include <cstdint>
#include <functional>

namespace dz {

// ─────────────────────────────────────────────────────────────────────────────
// Team
//
// Team 0 = unaffiliated (zombies, neutral objects).
// Teams 1-4 = player squads (2 players each, 4 teams max → 8 players total).
//
// Friendly-fire rule (enforced by DamageSystem on the server):
//   Same team → damage is blocked (friendly fire disabled).
//   Different teams → damage applies even when a temporary alliance is active
//                     (alliance only suppresses *targeting* AI, not hit boxes).
// ─────────────────────────────────────────────────────────────────────────────
enum class Team : uint8_t {
    Neutral = 0,
    Alpha   = 1,
    Bravo   = 2,
    Charlie = 3,
    Delta   = 4,
};

// ─────────────────────────────────────────────────────────────────────────────
// DamageType — drives death animations, loot, fire propagation checks
// ─────────────────────────────────────────────────────────────────────────────
enum class DamageType : uint8_t {
    Bullet,
    Melee,
    Explosion,
    Fire,
    Zombie,
    Fall,
};

// ─────────────────────────────────────────────────────────────────────────────
// HealthComponent
//
// Authority: server only.  Clients receive HP deltas via NetMessage and
// update a local copy for HUD display — never trust the client's own HP value.
// ─────────────────────────────────────────────────────────────────────────────
struct HealthComponent {
    // ── Core stats ───────────────────────────────────────────────────────────
    float maxHp     = 100.0f;
    float currentHp = 100.0f;
    float maxStamina = 100.0f;
    float currentStamina = 100.0f;

    /// Flat damage reduction (armor vest, etc.) — applied before HP loss.
    float armor     = 0.0f;

    /// Fire resistance [0, 1] — fraction of fire damage absorbed.
    float fireResist = 0.0f;

    // ── Team identity ─────────────────────────────────────────────────────────
    Team  team = Team::Neutral;

    // ── State ─────────────────────────────────────────────────────────────────
    bool  isAlive     = true;
    float deathTimer  = 5.0f;   ///< How long body persists after death
    bool  isInvincible= false;  ///< Used during respawn animation window
    float invincibleTimer = 0.0f; ///< Seconds of invincibility remaining

    /// Seconds remaining on a bleed-out timer (0 = not bleeding).
    float bleedTimer  = 0.0f;
    float bleedDps    = 0.0f;

    // ── Helpers ───────────────────────────────────────────────────────────────

    /// Returns actual HP removed after armor mitigation (≥ 0).
    float applyDamage(float rawDamage, DamageType type) noexcept {
        if (!isAlive || isInvincible) return 0.0f;

        float mitigated = rawDamage;
        if (type == DamageType::Fire)
            mitigated *= (1.0f - fireResist);
        mitigated = std::max(0.0f, mitigated - armor);

        currentHp -= mitigated;
        if (currentHp <= 0.0f) {
            currentHp = 0.0f;
            isAlive   = false;
        }
        return mitigated;
    }

    float heal(float amount) noexcept {
        if (!isAlive) return 0.0f;
        float actual = std::min(amount, maxHp - currentHp);
        currentHp   += actual;
        return actual;
    }

    float hpPercent() const noexcept {
        return (maxHp > 0.0f) ? (currentHp / maxHp) : 0.0f;
    }
};

} // namespace dz
