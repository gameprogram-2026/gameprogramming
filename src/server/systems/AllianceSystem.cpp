#include "AllianceSystem.h"
#include "shared/util/Logger.h"

namespace dz {

bool AllianceSystem::isAllied(uint8_t a, uint8_t b) const noexcept {
    if (a >= ALLIANCE_DIM || b >= ALLIANCE_DIM) return false;
    return m_ally[a][b] != 0;
}

bool AllianceSystem::proposeAlliance(uint8_t a, uint8_t b) {
    if (a == b || a == 0 || b == 0) return false;
    if (a >= ALLIANCE_DIM || b >= ALLIANCE_DIM) return false;
    if (isAllied(a, b)) return true; // already allied

    m_proposed[a][b] = 1;
    DZ_LOG_INFO("[Alliance] Team %u proposed truce to team %u", a, b);

    // Check if B has already proposed to A — handshake complete
    if (m_proposed[b][a]) {
        setAlliance(a, b, true);
        m_proposed[a][b] = 0;
        m_proposed[b][a] = 0;
        DZ_LOG_INFO("[Alliance] Truce ESTABLISHED: team %u ↔ team %u", a, b);
        broadcast(a, b, true);
        return true;
    }
    return false;
}

void AllianceSystem::handleBetrayal(uint8_t a, uint8_t b) {
    if (!isAllied(a, b)) return;
    DZ_LOG_INFO("[Alliance] BETRAYAL: team %u attacked allied team %u — alliance broken", a, b);
    setAlliance(a, b, false);
    broadcast(a, b, false);
}

void AllianceSystem::breakAlliance(uint8_t a, uint8_t b) {
    if (!isAllied(a, b)) return;
    DZ_LOG_INFO("[Alliance] Alliance broken voluntarily: team %u ↔ team %u", a, b);
    setAlliance(a, b, false);
    broadcast(a, b, false);
}

void AllianceSystem::setAlliance(uint8_t a, uint8_t b, bool active) {
    m_ally[a][b] = active ? 1 : 0;
    m_ally[b][a] = active ? 1 : 0;
}

void AllianceSystem::broadcast(uint8_t a, uint8_t b, bool active) {
    if (m_onBroadcast) m_onBroadcast(a, b, active);
}

} // namespace dz
