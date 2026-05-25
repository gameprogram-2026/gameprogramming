#pragma once
#include <cstdint>
#include <string>

namespace dz {

// ─────────────────────────────────────────────────────────────────────────────
// RenderLayer — draw order (painter's algorithm)
// ─────────────────────────────────────────────────────────────────────────────
enum class RenderLayer : uint8_t {
    Ground      = 0,   ///< Floor tiles, blood decals
    Objects     = 1,   ///< Barricades, loot crates, corpses
    Characters  = 2,   ///< Players, zombies
    Projectiles = 3,   ///< Bullets, grenades, flames
    Overlay     = 4,   ///< UI elements rendered in world space (health bars)
};

// ─────────────────────────────────────────────────────────────────────────────
// RenderComponent  (client-side only — server ignores this)
//
// References a texture by logical key (looked up in a TextureCache).
// Supports sprite-sheet animation via srcRect.
// ─────────────────────────────────────────────────────────────────────────────
struct RenderComponent {
    // ── Texture ───────────────────────────────────────────────────────────────
    std::string textureKey;     ///< Key into TextureCache, e.g. "player_idle"

    // ── Source rectangle inside the sprite sheet (pixels) ────────────────────
    int srcX = 0, srcY = 0;
    int srcW = 0, srcH = 0;     ///< 0 = use full texture dimensions

    // ── Destination size override (pixels) — 0 = use srcW/srcH ──────────────
    int dstW = 0, dstH = 0;

    // ── Tint / alpha ──────────────────────────────────────────────────────────
    uint8_t colorR = 255;
    uint8_t colorG = 255;
    uint8_t colorB = 255;
    uint8_t alpha  = 255;

    // ── Flip flags (matches SDL_RendererFlip) ─────────────────────────────────
    bool flipX = false;
    bool flipY = false;

    // ── Sorting ───────────────────────────────────────────────────────────────
    RenderLayer layer   = RenderLayer::Characters;
    int         zOffset = 0;    ///< Fine-grained sort within layer

    // ── Visibility ────────────────────────────────────────────────────────────
    bool visible = true;
};

// ─────────────────────────────────────────────────────────────────────────────
// AnimationComponent  (companion to RenderComponent)
//
// Cycles through frames on a sprite sheet row by row.
// ─────────────────────────────────────────────────────────────────────────────
struct AnimationComponent {
    std::string currentAnim;    ///< Key into an AnimationDef table
    int   frameCount   = 1;
    int   frameW       = 0;
    int   frameH       = 0;
    float frameDuration = 0.1f; ///< Seconds per frame
    float elapsed      = 0.0f;
    int   currentFrame = 0;
    bool  loop         = true;
    bool  playing      = true;
};

} // namespace dz
