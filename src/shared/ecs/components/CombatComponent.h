#pragma once
#include <cstdint>

namespace dz {

// ─────────────────────────────────────────────────────────────────────────────
// Noise radii in world units (pixels). 1 tile = 32 px ≈ 1 metre.
// ─────────────────────────────────────────────────────────────────────────────
constexpr float NOISE_WALK_RADIUS   =  48.0f;  // 1.5 m
constexpr float NOISE_RUN_RADIUS    = 160.0f;  // 5.0 m
constexpr float NOISE_MELEE_RADIUS  = 256.0f;  // 8.0 m
constexpr float NOISE_PISTOL_RADIUS = 960.0f;  // 30.0 m
constexpr float NOISE_RIFLE_RADIUS  =1280.0f;  // 40.0 m
constexpr float NOISE_EXPLOSION_RADIUS=2880.0f;// 90.0 m — triggers Frenzy

// ─────────────────────────────────────────────────────────────────────────────
// MeleeWeaponDef — hitbox and damage stats for melee attacks
// ─────────────────────────────────────────────────────────────────────────────
struct MeleeWeaponDef {
    float damage      = 35.0f;
    float range       = 72.0f;    ///< Reach in pixels — 2.25타일, 더 넉넉한 근접 범위
    float arcWidth    = 96.0f;    ///< Arc half-width — 좌우 48px
    float attackSpeed = 0.5f;     ///< Seconds per swing
    float bleedChance = 0.3f;     ///< Probability of inflicting bleed
    float bleedDps    = 5.0f;     ///< HP/s while bleeding
    float bleedDuration = 8.0f;   ///< Seconds bleed lasts (without bandage)
};

// ─────────────────────────────────────────────────────────────────────────────
// CombatComponent — per-entity combat state (server-authoritative)
// ─────────────────────────────────────────────────────────────────────────────
struct CombatComponent {
    // ── Melee state ───────────────────────────────────────────────────────────
    MeleeWeaponDef  melee;
    float           attackCooldown  = 0.0f;   ///< Seconds until next swing
    bool            isAttacking     = false;
    float           attackTimer     = 0.0f;   ///< Time within current swing

    // ── Ranged state ──────────────────────────────────────────────────────────
    float  fireCooldown   = 0.0f;
    int    ammoInMag      = 0;
    int    ammoReserve    = 0;
    int    magCapacity    = 7;     ///< Max rounds per magazine (pistol default)
    float  fireRate       = 0.3f;  ///< Seconds between shots
    float  reloadTime     = 2.0f;  ///< Seconds to reload
    float  reloadTimer    = 0.0f;
    bool   isReloading    = false;

    // ── Bleeding status effect ────────────────────────────────────────────────
    bool  isBleeding        = false;
    float bleedTimer        = 0.0f;   ///< Seconds remaining
    float bleedDps          = 5.0f;   ///< HP/s
    float bleedAccumulator  = 0.0f;   ///< Fractional HP owed

    // ── On-fire status effect ─────────────────────────────────────────────────
    bool  isOnFire          = false;
    float fireDamageTimer   = 0.0f;   ///< Time since last fire tick

    // ── Knockback ─────────────────────────────────────────────────────────────
    float knockVx = 0.0f;
    float knockVy = 0.0f;
    float knockTimer = 0.0f;

    // ── Noise emission this tick (set by action, read by NoiseSystem) ─────────
    float  noiseRadius   = 0.0f;
    uint8_t noiseCategory = 0;  // NoiseCategory enum value
    bool   hasNoise      = false;

    // ── Helpers ───────────────────────────────────────────────────────────────
    void emitNoise(float radius, uint8_t category) noexcept {
        if (radius > noiseRadius) {
            noiseRadius   = radius;
            noiseCategory = category;
            hasNoise      = true;
        }
    }
    void clearNoise() noexcept { hasNoise = false; noiseRadius = 0.0f; }

    void applyBleed(float dps, float duration) noexcept {
        isBleeding = true;
        bleedDps   = dps;
        bleedTimer = duration;
    }

    void cureBleed() noexcept {
        isBleeding        = false;
        bleedTimer        = 0.0f;
        bleedAccumulator  = 0.0f;
    }
};

} // namespace dz
