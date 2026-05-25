#pragma once
#include <cmath>

namespace dz {

// ─────────────────────────────────────────────────────────────────────────────
// TransformComponent
//
// World-space position, rotation (degrees), and uniform scale.
// The server treats (x, y) as tile-space floats; the client maps them to
// screen pixels using the camera transform in Renderer.
// ─────────────────────────────────────────────────────────────────────────────

struct TransformComponent {
    float x        = 0.0f;   ///< World X (pixels or tile-units × TILE_SIZE)
    float y        = 0.0f;   ///< World Y
    float rotation = 0.0f;   ///< Clockwise degrees [0, 360)
    float scaleX   = 1.0f;
    float scaleY   = 1.0f;

    // ── Velocity (integrated by MovementSystem each tick) ─────────────────────
    float vx = 0.0f;
    float vy = 0.0f;

    // ── Helpers ───────────────────────────────────────────────────────────────

    /// Direction vector derived from rotation.
    void forwardVector(float& outX, float& outY) const noexcept {
        const float rad = rotation * (3.14159265f / 180.0f);
        outX =  std::sin(rad);
        outY = -std::cos(rad);   // Y-axis is flipped in SDL screen space
    }

    float distanceTo(float ox, float oy) const noexcept {
        float dx = ox - x, dy = oy - y;
        return std::sqrt(dx * dx + dy * dy);
    }
};

} // namespace dz
