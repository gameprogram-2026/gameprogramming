#pragma once
#include <array>
#include <cstdint>
#include <string>

namespace dz {

// ─────────────────────────────────────────────────────────────────────────────
// NoiseCategory — used by the zombie AI to decide reaction speed
// ─────────────────────────────────────────────────────────────────────────────
enum class NoiseCategory : uint8_t {
    Silent      = 0,   ///< Crouch-walk, looting quietly
    Soft        = 1,   ///< Normal walk
    Moderate    = 2,   ///< Running, opening doors
    Loud        = 3,   ///< Gunshot, explosion, barricade construction
    Deafening   = 4,   ///< Grenade, vehicle crash — attracts all zombies on map
};

// ─────────────────────────────────────────────────────────────────────────────
// NoiseEvent — one frame's noise emission
// Server's NoiseSystem aggregates these and updates ZombieAI states.
// ─────────────────────────────────────────────────────────────────────────────
struct NoiseEvent {
    float          worldX    = 0.0f;
    float          worldY    = 0.0f;
    float          radius    = 0.0f;   ///< Detection radius in world units
    NoiseCategory  category  = NoiseCategory::Silent;
    float          decayRate = 1.0f;   ///< Radius shrinks by this per second
    float          ttl       = 0.5f;   ///< Seconds until event expires
};

// ─────────────────────────────────────────────────────────────────────────────
// SoundEmitterComponent
//
// Dual purpose:
//   • Server-side: generates NoiseEvents that feed into zombie AI
//   • Client-side: triggers SDL2_mixer playback via AudioManager
//
// Design rule: the server is authoritative on *what* noise was made and *where*.
// The client maps noise events to audio cues for the local player.
// ─────────────────────────────────────────────────────────────────────────────

constexpr int MAX_ACTIVE_NOISES = 8;

struct SoundEmitterComponent {
    // ── Client audio ──────────────────────────────────────────────────────────
    /// Key into SoundCache (e.g. "gunshot_pistol", "footstep_gravel").
    /// Set by the action system each frame a sound should play.
    std::string pendingSoundKey;
    float       volume      = 1.0f;   ///< [0.0, 1.0]
    bool        isLooping   = false;
    int         channelID   = -1;     ///< SDL_mixer channel (-1 = auto-assign)

    // ── Server noise simulation ───────────────────────────────────────────────
    /// Ring buffer of active noise events emitted by this entity this tick.
    std::array<NoiseEvent, MAX_ACTIVE_NOISES> activeNoises{};
    int noiseCount = 0;

    /// Noise radii by action — tuned in data/sounds.json, applied at runtime.
    float footstepRadiusWalk  = 80.0f;
    float footstepRadiusSprint= 200.0f;
    float gunshotRadius       = 600.0f;
    float meleeRadius         = 60.0f;
    float buildRadius         = 300.0f;

    // ── Helpers ───────────────────────────────────────────────────────────────
    void emitNoise(float wx, float wy, float radius, NoiseCategory cat,
                   float ttl = 0.5f) {
        if (noiseCount >= MAX_ACTIVE_NOISES) return;
        auto& ev    = activeNoises[noiseCount++];
        ev.worldX   = wx;
        ev.worldY   = wy;
        ev.radius   = radius;
        ev.category = cat;
        ev.ttl      = ttl;
    }

    void clearNoises() noexcept { noiseCount = 0; }
};

} // namespace dz
