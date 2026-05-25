#include "ExtractionSystem.h"
#include "shared/ecs/components/TransformComponent.h"
#include "shared/ecs/components/HealthComponent.h"
#include "shared/ecs/components/InventoryComponent.h"
#include "shared/ecs/components/NetworkComponent.h"
#include "shared/util/Logger.h"
#include "server/ZombieAI.h"
#include <cmath>

namespace dz {

void ExtractionSystem::addZone(const ExtractionZone& zone) {
    if (m_zoneCount < MAX_EXTRACTION_ZONES)
        m_zones[m_zoneCount++] = zone;
}

// ─────────────────────────────────────────────────────────────────────────────
// update — called every server tick
// ─────────────────────────────────────────────────────────────────────────────
void ExtractionSystem::update(World& world, float dt, float gameTime) {
    // 5분 경과 시 탈출존 오픈 (기획서 2.4)
    if (!m_zonesOpen && gameTime >= EXTRACTION_OPEN_DELAY) {
        m_zonesOpen = true;
        for (int i = 0; i < m_zoneCount; ++i) m_zones[i].open = true;
        DZ_LOG_INFO("[Extract] 탈출 지점 활성화! (게임 시작 5분 경과)");
        if (m_onExtractionOpen) m_onExtractionOpen();
        
        // 모든 좀비를 광란(Frenzy) 상태로 깨움
        auto& aiPool = world.pool<ZombieAIComponent>();
        for (size_t i = 0; i < aiPool.owners().size(); ++i) {
            auto& ai = aiPool.data()[i];
            ai.state = ZombieState::Frenzy;
            ai.alertLevel = 1.0f;
            ai.stateTimer = 0.0f;
            ai.chainTriggered = false;
        }
    }
    if (!m_zonesOpen) return; // 아직 비활성
    for (EntityID id : world.alive()) {
        Entity e{id};
        auto* xf  = world.tryGet<TransformComponent>(e);
        auto* hp  = world.tryGet<HealthComponent>(e);
        auto* net = world.tryGet<NetworkComponent>(e);
        if (!xf || !hp || !net) continue;
        if (net->role != NetRole::LocallyOwned &&
            net->role != NetRole::ServerAuth) continue; // skip zombies/buildings
        if (hp->team == Team::Neutral) continue;

        auto& st = stateFor(id);

        // ── Death check ────────────────────────────────────────────────────────
        if (!hp->isAlive) {
            // 사망 시 채널링 즉시 취소
            st = {};
            continue;
        }

        // ── Check if inside an extraction zone ────────────────────────────────
        int8_t zone = findZone(xf->x, xf->y);
        if (zone < 0) {
            st.channeling   = false;
            st.channelTimer = 0.0f;
            continue;
        }

        // ── Movement check — reset if player moved > 4 px ─────────────────────
        float moveDist = std::sqrt(std::pow(xf->x - st.lastX, 2.0f) +
                                   std::pow(xf->y - st.lastY, 2.0f));
        bool moved = (st.channeling && moveDist > 4.0f);

        // ── Damage check — reset if HP dropped ────────────────────────────────
        bool tookDamage = (st.channeling && hp->currentHp < st.lastHp - 0.5f);

        if (moved || tookDamage) {
            DZ_LOG_DEBUG("[Extract] Player %u channel interrupted (%s)",
                net->netID, moved ? "moved" : "took damage");
            st.channeling   = false;
            st.channelTimer = 0.0f;
        }

        // ── Accumulate channel time ───────────────────────────────────────────
        if (st.channeling) {
            st.channelTimer += dt;
            st.lastX = xf->x;
            st.lastY = xf->y;
            st.lastHp = hp->currentHp;

            // ── Extraction complete ───────────────────────────────────────────────
            if (st.channelTimer >= EXTRACTION_CHANNEL_TIME) {
                DZ_LOG_INFO("[Extract] Player %u EXTRACTED via zone %d — inventory preserved!",
                            net->netID, zone);
                st.channeling   = false;
                st.channelTimer = 0.0f;
                if (m_onExtracted) m_onExtracted(e, static_cast<uint8_t>(zone));
            }
        }
    }
}

bool ExtractionSystem::startChanneling(uint32_t netID, float wx, float wy, float currentHp) {
    if (!m_zonesOpen) return false;
    
    int8_t zone = findZone(wx, wy);
    if (zone < 0) return false;

    auto& st = stateFor(netID);
    if (!st.channeling) {
        st.channeling   = true;
        st.channelTimer = 0.0f;
        st.zoneID       = static_cast<uint8_t>(zone);
        st.lastX        = wx;
        st.lastY        = wy;
        st.lastHp       = currentHp;
        DZ_LOG_INFO("[Extract] Player %u started channeling zone %d (Nakwon style)", netID, zone);
        return true;
    }
    return false;
}

int8_t ExtractionSystem::findZone(float wx, float wy) const noexcept {
    for (int i = 0; i < m_zoneCount; ++i) {
        if (!m_zones[i].open) continue;
        float dx = wx - m_zones[i].cx;
        float dy = wy - m_zones[i].cy;
        if (std::sqrt(dx*dx + dy*dy) <= EXTRACTION_ZONE_RADIUS)
            return static_cast<int8_t>(i);
    }
    return -1;
}

PlayerExtractionState& ExtractionSystem::stateFor(uint32_t entityID) {
    for (auto& entry : m_states) {
        if (entry.entityID == entityID) return entry.state;
    }
    // Find empty slot
    for (auto& entry : m_states) {
        if (entry.entityID == 0) {
            entry.entityID = entityID;
            return entry.state;
        }
    }
    // Overflow — reuse first slot (shouldn't happen with ≤8 players)
    m_states[0].entityID = entityID;
    m_states[0].state    = {};
    return m_states[0].state;
}

float ExtractionSystem::channelProgress(uint32_t netID) const {
    for (const auto& entry : m_states) {
        if (entry.entityID == netID)
            return entry.state.channelTimer / EXTRACTION_CHANNEL_TIME;
    }
    return 0.0f;
}

} // namespace dz
