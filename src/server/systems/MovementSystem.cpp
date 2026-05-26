#include "MovementSystem.h"
#include "shared/ecs/components/TransformComponent.h"
#include "shared/ecs/components/HealthComponent.h"
#include "shared/ecs/components/NetworkComponent.h"
#include "shared/ecs/components/CombatComponent.h"
#include "shared/network/Protocol.h"
#include "shared/network/Packet.h"
#include "shared/util/Logger.h"
#include <cmath>

namespace dz {

void MovementSystem::update(World& world, const TileMap& map, float dt) {
    // MovementSystem::applyInput is called per-player by GameServer when inputs arrive.
    // This update pass handles entities with autonomous motion (knockback, etc.).
    auto& combatPool = world.pool<CombatComponent>();
    auto& xfPool     = world.pool<TransformComponent>();

    for (size_t i = 0; i < combatPool.owners().size(); ++i) {
        Entity e{combatPool.owners()[i]};
        auto& cbt = combatPool.data()[i];
        auto* xf  = xfPool.get(e.id);
        if (!xf) continue;

        // ── Apply knockback ────────────────────────────────────────────────────
        if (cbt.knockTimer > 0.0f) {
            xf->x += cbt.knockVx * dt;
            xf->y += cbt.knockVy * dt;
            cbt.knockTimer -= dt;
            cbt.knockVx *= (1.0f - 10.0f * dt); // dampen
            cbt.knockVy *= (1.0f - 10.0f * dt);
            map.resolveAABB(xf->x, xf->y, ENTITY_HW, ENTITY_HH);
        }

        // ── Cooldown ticks ────────────────────────────────────────────────────
        if (cbt.attackCooldown > 0.0f) cbt.attackCooldown -= dt;
        if (cbt.fireCooldown   > 0.0f) cbt.fireCooldown   -= dt;
        if (cbt.reloadTimer    > 0.0f) {
            cbt.reloadTimer -= dt;
            if (cbt.reloadTimer <= 0.0f) {
                cbt.reloadTimer = 0.0f;
                cbt.isReloading = false;
                int need = cbt.magCapacity - cbt.ammoInMag;
                if (need > 0 && cbt.ammoReserve > 0) {
                    int fill     = (need < cbt.ammoReserve) ? need : cbt.ammoReserve;
                    cbt.ammoInMag    += fill;
                    cbt.ammoReserve  -= fill;
                }
            }
        }
    }
}

void MovementSystem::applyInput(World& world, const TileMap& map,
                                 uint32_t ownerID, const InputPacket& input,
                                 float dt) {
    // Find the entity owned by this peer
    for (EntityID id : world.alive()) {
        Entity e{id};
        auto* net = world.tryGet<NetworkComponent>(e);
        if (!net || net->ownerID != ownerID) continue;

        auto* xf  = world.tryGet<TransformComponent>(e);
        auto* hp  = world.tryGet<HealthComponent>(e);
        auto* cbt = world.tryGet<CombatComponent>(e);
        if (!xf || !hp || !hp->isAlive) return;

        // ── Choose speed ───────────────────────────────────────────────────────
        float speed = SPEED_WALK;
        bool sprinting = (input.actions & ACT_SPRINT) && !(input.actions & ACT_CROUCH);
        bool crouching = (input.actions & ACT_CROUCH) != 0;
        if (sprinting) speed = SPEED_SPRINT;
        if (crouching) speed = SPEED_CROUCH;

        // ── Normalize and apply movement ───────────────────────────────────────
        float mx = input.moveX, my = input.moveY;
        if (!std::isfinite(mx) || !std::isfinite(my)) {
            mx = 0.0f; my = 0.0f;
        }
        float len = std::sqrt(mx * mx + my * my);
        if (len > 0.01f) { mx /= len; my /= len; }

        float prevX = xf->x, prevY = xf->y;
        // 축 분리 이동: X 이동→X 충돌, Y 이동→Y 충돌
        // 동시 해결 시 벽 경계에서 lateral warp 발생 방지
        xf->x += mx * speed * dt;
        map.resolveAxisX(xf->x, xf->y, ENTITY_HW, ENTITY_HH);
        xf->y += my * speed * dt;
        map.resolveAxisY(xf->x, xf->y, ENTITY_HW, ENTITY_HH);

        // 이동 확인 로그 (처음 100틱만)
        static uint32_t s_logCount = 0;
        if (len > 0.01f && s_logCount < 100) {
            ++s_logCount;
            DZ_LOG_DEBUG("[Move] peer=%u move=(%.2f,%.2f) pos=(%.1f,%.1f)->(%.1f,%.1f)",
                         ownerID, mx, my, prevX, prevY, xf->x, xf->y);
        }

        // ── Update facing rotation ─────────────────────────────────────────────
        xf->rotation = input.aimAngle;

        // ── Emit footstep noise ────────────────────────────────────────────────
        if (cbt && (len > 0.01f)) {
            float noiseR = sprinting ? NOISE_RUN_RADIUS : NOISE_WALK_RADIUS;
            uint8_t cat  = sprinting ? 2 : 1; // Moderate : Soft
            if (crouching) { noiseR = 0.0f; cat = 0; } // Silent
            cbt->emitNoise(noiseR, cat);
        }

        // Mark dirty for snapshot
        if (net) net->markDirty(DIRTY_TRANSFORM);
        return;
    }
}

} // namespace dz
