#include "NoiseSystem.h"
#include "shared/ecs/components/TransformComponent.h"
#include "shared/ecs/components/CombatComponent.h"
#include <cmath>
#include <algorithm>

namespace dz {

void NoiseSystem::update(World& world, float dt) {
    // Decay existing events
    for (auto& ev : m_events) ev.ttl -= dt;
    m_events.erase(
        std::remove_if(m_events.begin(), m_events.end(),
                       [](const WorldNoiseEvent& e) { return e.ttl <= 0.0f; }),
        m_events.end());

    // Harvest noise from entities that emitted this tick
    auto& cbtPool = world.pool<CombatComponent>();
    auto& xfPool  = world.pool<TransformComponent>();

    for (size_t i = 0; i < cbtPool.owners().size(); ++i) {
        auto& cbt = cbtPool.data()[i];
        if (!cbt.hasNoise) { cbt.clearNoise(); continue; }

        auto* xf = xfPool.get(cbtPool.owners()[i]);
        if (xf) {
            m_events.push_back({xf->x, xf->y, cbt.noiseRadius, cbt.noiseCategory, 0.6f});
        }
        cbt.clearNoise();
    }
}

void NoiseSystem::addEvent(float x, float y, float radius, uint8_t category, float ttl) {
    m_events.push_back({x, y, radius, category, ttl});
}

uint8_t NoiseSystem::maxCategoryNear(float cx, float cy, float radius) const noexcept {
    uint8_t maxCat = 0;
    for (const auto& ev : m_events) {
        float dx = ev.x - cx, dy = ev.y - cy;
        float dist = std::sqrt(dx*dx + dy*dy);
        if (dist <= ev.radius + radius) {
            if (ev.category > maxCat) maxCat = ev.category;
        }
    }
    return maxCat;
}

} // namespace dz
