#pragma once
#include <cstdint>
#include <bitset>
#include <string>

namespace dz {

// ─────────────────────────────────────────────────────────────────────────────
// NetRole — determines who simulates this entity
// ─────────────────────────────────────────────────────────────────────────────
enum class NetRole : uint8_t {
    /// Entity exists only on the server (bullets, fire tiles, zombie AI state)
    ServerOnly    = 0,
    /// Simulated on server; clients receive state snapshots
    ServerAuth    = 1,
    /// Controlled by one specific client; that client predicts movement,
    /// server reconciles each tick
    LocallyOwned  = 2,
    /// Simulated by another client; this client interpolates snapshots
    RemoteProxy   = 3,
};

// ─────────────────────────────────────────────────────────────────────────────
// Dirty flags — bit indices for fields that need to be synced this tick
// ─────────────────────────────────────────────────────────────────────────────
enum DirtyFlag : uint8_t {
    DIRTY_TRANSFORM  = 0,
    DIRTY_HEALTH     = 1,
    DIRTY_INVENTORY  = 2,
    DIRTY_ANIMATION  = 3,
    DIRTY_FIRE       = 4,   ///< Fire tile spread delta
    DIRTY_COUNT      = 8,   ///< Keep ≤ 8 to fit in one byte
};

// ─────────────────────────────────────────────────────────────────────────────
// NetworkComponent
//
// Authority model:
//   • Server owns ALL state — it is the single source of truth.
//   • Clients send InputPackets (direction, actions) at ~64 Hz.
//   • Server sends WorldStateSnapshot at ~20 Hz to all clients.
//   • For the locally-owned player the client also runs client-side prediction
//     (moves entity immediately, rolls back on server correction).
//
// The NetworkComponent is present on every replicated entity in both the
// server's World and the client's World.
// ─────────────────────────────────────────────────────────────────────────────
struct NetworkComponent {
    // ── Identity ──────────────────────────────────────────────────────────────
    uint32_t netID    = 0;    ///< Globally unique, assigned by server on spawn
    uint32_t ownerID  = 0xFFFFFFFF; ///< Peer/player ID who controls this entity (0xFFFFFFFF=server)
    NetRole  role     = NetRole::ServerAuth;

    // ── Sync ──────────────────────────────────────────────────────────────────
    std::bitset<DIRTY_COUNT> dirtyFlags;

    void markDirty(DirtyFlag flag) noexcept { dirtyFlags.set(flag); }
    void clearDirty()              noexcept { dirtyFlags.reset(); }
    bool isDirty(DirtyFlag flag)   const noexcept { return dirtyFlags.test(flag); }
    bool hasAnyDirty()             const noexcept { return dirtyFlags.any(); }

    // ── Snapshot sequencing ───────────────────────────────────────────────────
    uint32_t lastSnapshotTick = 0;  ///< Last tick this entity was included in a snapshot

    // ── Client-side prediction (locally owned entities only) ──────────────────
    uint32_t lastAckedInputSeq = 0;    ///< Last input sequence the server acknowledged

    // ── Interpolation (remote proxies only) ───────────────────────────────────
    /// Position buffered from the two most recent snapshots for lerp.
    float snapX0 = 0.0f, snapY0 = 0.0f;  ///< Older snapshot
    float snapX1 = 0.0f, snapY1 = 0.0f;  ///< Newer snapshot
    float snapT  = 0.0f;                  ///< Current lerp t ∈ [0, 1]
    float snapDt = 0.1f;                  ///< Time between snapshots (seconds)
};

} // namespace dz
