#pragma once
#include <cstdint>
#include <array>
#include <functional>
#include "shared/network/Protocol.h"

namespace dz {

// ─────────────────────────────────────────────────────────────────────────────
// AllianceSystem — manages team truce flags (server-authoritative)
//
// Rules:
//   • Two teams can establish a temporary truce (both must agree).
//   • During truce: zombie AI does not target either team on behalf of the other.
//     Damage hit boxes STILL APPLY — players can and do betray.
//   • Betrayal: any damage event between allied teams → immediate flag clear
//     + broadcast to all clients.
//   • Alliance matrix: ally[A][B] == 1 iff teams A and B are in truce.
// ─────────────────────────────────────────────────────────────────────────────

constexpr int ALLIANCE_DIM = MAX_TEAMS + 1; // index 0 unused (Neutral)

class AllianceSystem {
public:
    using BroadcastCB = std::function<void(uint8_t teamA, uint8_t teamB, bool active)>;

    void onBroadcast(BroadcastCB cb) { m_onBroadcast = std::move(cb); }

    // ── Queries ───────────────────────────────────────────────────────────────
    bool isAllied(uint8_t teamA, uint8_t teamB) const noexcept;
    const uint8_t* matrix() const noexcept { return m_ally[0].data(); }

    // ── Mutations ─────────────────────────────────────────────────────────────

    /// Team A proposes truce to team B.
    /// Returns true once B has also proposed (handshake), establishing alliance.
    bool proposeAlliance(uint8_t teamA, uint8_t teamB);

    /// Called when a damage event is detected between two allied teams.
    /// Breaks the alliance immediately and broadcasts.
    void handleBetrayal(uint8_t teamA, uint8_t teamB);

    /// Explicit alliance break request (e.g. player presses "Break Truce").
    void breakAlliance(uint8_t teamA, uint8_t teamB);

private:
    // Symmetric proposal matrix — proposal[A][B]=1 means A proposed to B
    std::array<std::array<uint8_t, ALLIANCE_DIM>, ALLIANCE_DIM> m_proposed{};
    // Active alliance matrix — symmetric
    std::array<std::array<uint8_t, ALLIANCE_DIM>, ALLIANCE_DIM> m_ally{};

    BroadcastCB m_onBroadcast;

    void setAlliance(uint8_t a, uint8_t b, bool active);
    void broadcast(uint8_t a, uint8_t b, bool active);
};

} // namespace dz
