#include "CombatSystem.h"
#include "shared/ecs/components/TransformComponent.h"
#include "shared/ecs/components/CombatComponent.h"
#include "shared/ecs/components/NetworkComponent.h"
#include "shared/ecs/components/InventoryComponent.h"
#include "shared/network/Protocol.h"
#include "shared/util/Logger.h"
#include "server/ZombieAI.h"
#include <cmath>
#include <cstdlib>

namespace dz {

// ─────────────────────────────────────────────────────────────────────────────
// update — tick bleeding, fire damage, attack cooldowns
// ─────────────────────────────────────────────────────────────────────────────
void CombatSystem::update(World& world, float dt) {
    tickBleeding(world, dt);
    tickFireDamage(world, dt);
    tickStamina(world, dt);

    auto& hpPool = world.pool<HealthComponent>();
    for (size_t i = 0; i < hpPool.owners().size(); ++i) {
        auto& hp = hpPool.data()[i];
        if (hp.invincibleTimer > 0.0f) {
            hp.invincibleTimer -= dt;
            hp.isInvincible = true;
            if (hp.invincibleTimer <= 0.0f) {
                hp.invincibleTimer = 0.0f;
                hp.isInvincible = false;
            }
        }
    }
}

void CombatSystem::tickBleeding(World& world, float dt) {
    auto& cbtPool = world.pool<CombatComponent>();
    auto& hpPool  = world.pool<HealthComponent>();

    for (size_t i = 0; i < cbtPool.owners().size(); ++i) {
        auto& cbt = cbtPool.data()[i];
        if (!cbt.isBleeding) continue;

        Entity e{cbtPool.owners()[i]};
        auto* hp = hpPool.get(e.id);
        if (!hp || !hp->isAlive) continue;

        cbt.bleedTimer        -= dt;
        cbt.bleedAccumulator  += cbt.bleedDps * dt;

        // Apply accumulated damage in integer chunks to avoid float drift
        while (cbt.bleedAccumulator >= 1.0f) {
            float dmg = hp->applyDamage(1.0f, DamageType::Melee);
            cbt.bleedAccumulator -= 1.0f;
            (void)dmg;
        }

        if (cbt.bleedTimer <= 0.0f) {
            cbt.cureBleed();
        }

        if (!hp->isAlive) {
            DZ_LOG_INFO("[Combat] Entity %u bled out", e.id);
            if (m_onDeath) m_onDeath(e, Entity{NULL_ENTITY}, DamageType::Melee);
        }
    }
}

void CombatSystem::tickFireDamage(World& world, float dt) {
    constexpr float FIRE_TICK_INTERVAL = 0.5f;
    constexpr float FIRE_DPS           = 15.0f;

    auto& cbtPool = world.pool<CombatComponent>();
    for (size_t i = 0; i < cbtPool.owners().size(); ++i) {
        auto& cbt = cbtPool.data()[i];
        if (!cbt.isOnFire) continue;

        Entity e{cbtPool.owners()[i]};
        auto* hp = world.tryGet<HealthComponent>(e);
        if (!hp || !hp->isAlive) continue;

        cbt.fireDamageTimer += dt;
        if (cbt.fireDamageTimer >= FIRE_TICK_INTERVAL) {
            cbt.fireDamageTimer -= FIRE_TICK_INTERVAL;
            hp->applyDamage(FIRE_DPS * FIRE_TICK_INTERVAL, DamageType::Fire);
            if (!hp->isAlive && m_onDeath)
                m_onDeath(e, Entity{NULL_ENTITY}, DamageType::Fire);
        }
    }
}

void CombatSystem::tickStamina(World& world, float dt) {
    auto& hpPool = world.pool<HealthComponent>();
    for (size_t i = 0; i < hpPool.owners().size(); ++i) {
        auto& hp = hpPool.data()[i];
        if (!hp.isAlive) continue;
        
        if (hp.currentStamina < hp.maxStamina) {
            hp.currentStamina += 15.0f * dt; // 초당 15 회복
            if (hp.currentStamina > hp.maxStamina) {
                hp.currentStamina = hp.maxStamina;
            }
            
            // 변경되었으니 클라이언트 동기화 필요
            Entity e{hpPool.owners()[i]};
            auto* net = world.tryGet<NetworkComponent>(e);
            if (net) {
                // stamina 변경은 너무 자주 일어나므로 매 틱 HP 패킷을 보내는 것은 낭비일 수 있음.
                // 하지만 현재 HpSync를 DIRTY_HEALTH로 모아서 보내고 있으므로, dirty mark를 남기거나, 
                // 어차피 클라이언트에서 로컬 보간을 하거나 해야함. 일단 간단히 dirty mark.
                // DIRTY_HEALTH는 약간 부담될 수 있으나, 일단 적용.
                net->markDirty(DIRTY_HEALTH);
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// tryMeleeAttack — AABB sweep from attacker's front
// ─────────────────────────────────────────────────────────────────────────────
bool CombatSystem::tryMeleeAttack(World& world, Entity attacker) {
    auto* axf  = world.tryGet<TransformComponent>(attacker);
    auto* acbt = world.tryGet<CombatComponent>(attacker);
    auto* ahp  = world.tryGet<HealthComponent>(attacker);
    if (!axf || !acbt || !ahp || !ahp->isAlive) return false;
    if (acbt->attackCooldown > 0.0f) return false;

    // 스태미나 소모 (15)
    if (ahp->currentStamina < 15.0f) return false;
    ahp->currentStamina -= 15.0f;
    
    // Reset cooldown
    acbt->attackCooldown = acbt->melee.attackSpeed;
    acbt->emitNoise(NOISE_MELEE_RADIUS, 2); // Moderate noise

    // Build hitbox in front of attacker using aim rotation
    float rad = axf->rotation * (3.14159265f / 180.0f);
    float fwdX =  std::sin(rad);
    float fwdY = -std::cos(rad);

    float hitCX = axf->x + fwdX * (acbt->melee.range * 0.5f);
    float hitCY = axf->y + fwdY * (acbt->melee.range * 0.5f);
    float hw    = acbt->melee.arcWidth  * 0.5f;
    float hh    = acbt->melee.range     * 0.5f;

    bool hitAny = false;
    for (EntityID vid : world.alive()) {
        Entity victim{vid};
        if (victim.id == attacker.id) continue;

        auto* vxf = world.tryGet<TransformComponent>(victim);
        auto* vhp = world.tryGet<HealthComponent>(victim);
        if (!vxf || !vhp || !vhp->isAlive) continue;

        // Local space transformation for rotated hitbox (OBB)
        float vx = vxf->x - hitCX;
        float vy = vxf->y - hitCY;
        
        float cosA = std::cos(-rad);
        float sinA = std::sin(-rad);
        
        float localX = vx * cosA - vy * sinA;
        float localY = vx * sinA + vy * cosA;

        if (std::abs(localX) > hw || std::abs(localY) > hh) continue;

        // Friendly-fire check
        if (ahp->team == vhp->team && ahp->team != Team::Neutral) continue;

        auto result = applyDamage(world, victim, attacker,
                                   acbt->melee.damage, DamageType::Melee);

        // Bleed chance
        auto* vcbt = world.tryGet<CombatComponent>(victim);
        if (vcbt && !vcbt->isBleeding) {
            float roll = static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
            if (roll < acbt->melee.bleedChance)
                vcbt->applyBleed(acbt->melee.bleedDps, acbt->melee.bleedDuration);
        }

        // Knockback (Brute는 면역)
        if (vcbt) {
            bool isBrute = false;
            auto* zai = world.tryGet<ZombieAIComponent>(victim);
            if (zai && zai->type == ZombieType::Brute) isBrute = true;
            
            if (!isBrute) {
                vcbt->knockVx = fwdX * 200.0f;
                vcbt->knockVy = fwdY * 200.0f;
                vcbt->knockTimer = 0.15f;
            }
        }

        // m_onDamage는 applyDamage 내부에서 이미 호출됨
        hitAny = true;
    }
    return hitAny;
}

// ─────────────────────────────────────────────────────────────────────────────
// applyDamage — central damage application (enforces FF rules, broadcasts)
// ─────────────────────────────────────────────────────────────────────────────
DamageResult CombatSystem::applyDamage(World& world, Entity victim, Entity attacker,
                                        float rawDamage, DamageType type,
                                        const uint8_t* allianceMatrix) {
    DamageResult res{};
    res.type = type;

    auto* vhp = world.tryGet<HealthComponent>(victim);
    auto* ahp = attacker.isValid() ? world.tryGet<HealthComponent>(attacker) : nullptr;
    if (!vhp) return res;

    auto* vnet = world.tryGet<NetworkComponent>(victim);
    auto* anet = attacker.isValid() ? world.tryGet<NetworkComponent>(attacker) : nullptr;

    res.victimID   = vnet ? vnet->netID : 0;
    res.attackerID = anet ? anet->netID : 0;

    // Friendly-fire guard (within-team — now allowed for PvP!)
    // if (ahp && ahp->team == vhp->team && ahp->team != Team::Neutral) {
    //     return res;
    // }

    bool wasAlive = vhp->isAlive;
    float dealt     = vhp->applyDamage(rawDamage, type);
    res.damage      = dealt;
    res.remainingHp = vhp->currentHp;
    res.killed      = (wasAlive && !vhp->isAlive);

    if (vnet) vnet->markDirty(DIRTY_HEALTH);

    // 데미지 이벤트 브로드캐스트 — 모든 공격 경로에서 호출
    // (좀비 공격, 총기, 폭발, 직접 applyDamage 호출 전부 커버)
    if (res.damage > 0.0f && m_onDamage) m_onDamage(res);

    if (res.killed) {
        DZ_LOG_INFO("[Combat] Entity %u killed entity %u (%s)",
            res.attackerID, res.victimID,
            type == DamageType::Fire ? "fire" : "damage");
        if (m_onDeath) m_onDeath(victim, attacker, type);
    }
    return res;
}

// ─────────────────────────────────────────────────────────────────────────────
// triggerExplosion — radius damage from turret oil explosion
// ─────────────────────────────────────────────────────────────────────────────
void CombatSystem::triggerExplosion(World& world, float wx, float wy,
                                     float radius, float damage, Entity source) {
    DZ_LOG_INFO("[Combat] Explosion at (%.0f, %.0f) r=%.0f dmg=%.0f",
                wx, wy, radius, damage);

    for (EntityID id : world.alive()) {
        Entity e{id};
        if (e.id == source.id) continue;
        auto* xf = world.tryGet<TransformComponent>(e);
        auto* hp = world.tryGet<HealthComponent>(e);
        if (!xf || !hp || !hp->isAlive) continue;

        float dx = xf->x - wx, dy = xf->y - wy;
        float dist = std::sqrt(dx*dx + dy*dy);
        if (dist > radius) continue;

        // Falloff: full damage at centre, half at edge
        float falloff = 1.0f - (dist / radius) * 0.5f;
        auto result = applyDamage(world, e, source, damage * falloff,
                                   DamageType::Explosion);
        // m_onDamage is already fired inside applyDamage

        // Knockback from blast
        auto* cbt = world.tryGet<CombatComponent>(e);
        if (cbt && dist > 0.001f) {
            float nx = dx / dist, ny = dy / dist;
            cbt->knockVx = nx * 400.0f * falloff;
            cbt->knockVy = ny * 400.0f * falloff;
            cbt->knockTimer = 0.3f;
        }
    }
}

} // namespace dz
