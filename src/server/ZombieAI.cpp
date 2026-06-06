#include "ZombieAI.h"
#include "shared/ecs/components/TransformComponent.h"
#include "shared/ecs/components/HealthComponent.h"
#include "shared/ecs/components/NetworkComponent.h"
#include "shared/ecs/components/CombatComponent.h"
#include "shared/ecs/components/BuildingComponent.h"
#include "shared/TileMap.h"
#include "shared/util/Logger.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace dz {

// ─────────────────────────────────────────────────────────────────────────────
// update — main entry: iterate all zombie entities
// ─────────────────────────────────────────────────────────────────────────────
void ZombieAISystem::update(World& world, const NoiseSystem& noise, float dt, float gameTime) {
    auto& aiPool = world.pool<ZombieAIComponent>();
    for (size_t i = 0; i < aiPool.owners().size(); ++i) {
        Entity zombie{aiPool.owners()[i]};
        auto& ai = aiPool.data()[i];

        auto* hp = world.tryGet<HealthComponent>(zombie);
        if (!hp || !hp->isAlive) {
            ai.state = ZombieState::Dead;
            continue;
        }

        updateFSM(world, zombie, ai, noise, dt, gameTime);
        doMovement(world, zombie, ai, dt);
        doAttack(world, zombie, ai, dt);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// updateFSM — state transitions based on noise
// ─────────────────────────────────────────────────────────────────────────────
void ZombieAISystem::updateFSM(World& world, Entity zombie,
                                ZombieAIComponent& ai,
                                const NoiseSystem& noise, float dt, float gameTime) {
    auto* xf = world.tryGet<TransformComponent>(zombie);
    if (!xf) return;

    float timeOfDay = std::fmod(gameTime, 180.0f);
    bool isNight = timeOfDay > 120.0f;

    bool doorTargetActive = false;
    if (m_map && ai.targetDoorID >= 0) {
        const auto& doors = m_map->getDoors();
        const uint16_t doorID = static_cast<uint16_t>(ai.targetDoorID);
        if (doorID < doors.size() && !doors[doorID].open && !doors[doorID].broken) {
            float doorX = TileMap::tileCentre(doors[doorID].tx);
            float doorY = TileMap::tileCentre(doors[doorID].ty);
            float ddx = doorX - xf->x, ddy = doorY - xf->y;
            float doorDist2 = ddx*ddx + ddy*ddy;

            // 플레이어가 문보다 더 가까우면 문 타겟 해제 (플레이어 우선)
            Entity nearest = findNearestEnemy(world, zombie);
            bool playerCloser = false;
            if (nearest.isValid()) {
                auto* nxf = world.tryGet<TransformComponent>(nearest);
                if (nxf) {
                    float pdx = nxf->x - xf->x, pdy = nxf->y - xf->y;
                    if (pdx*pdx + pdy*pdy < doorDist2) playerCloser = true;
                }
            }

            if (!playerCloser) {
                ai.targetX = doorX;
                ai.targetY = doorY;
                doorTargetActive = true;
            }
        } else {
            ai.targetDoorID = -1;
        }
    }

    // Sample loudest noise within hearing range and keep its position. State changes
    // from noise must have a real destination; otherwise zombies can chase (0,0).
    // Detection: zombie hears the sound if it's within the noise event's own radius.
    uint8_t maxNoise = 0;
    float noiseTargetX = xf->x;
    float noiseTargetY = xf->y;
    for (const auto& ev : noise.events()) {
        const float dx = ev.x - xf->x;
        const float dy = ev.y - xf->y;
        const float dist = std::sqrt(dx * dx + dy * dy);
        if (dist > ev.radius) continue;
        if (ev.category > maxNoise) {
            maxNoise = ev.category;
            noiseTargetX = ev.x;
            noiseTargetY = ev.y;
        }
    }

    ai.stateTimer += dt;

    // ── 시야 범위 내 플레이어 직접 감지 (소음 무관) ──────────────────────────
    if (!doorTargetActive) {
        Entity nearest = findNearestEnemy(world, zombie);
        if (nearest.isValid()) {
            auto* nxf = world.tryGet<TransformComponent>(nearest);
            auto* ncbt = world.tryGet<CombatComponent>(nearest);
            if (nxf) {
                float dx = nxf->x - xf->x, dy = nxf->y - xf->y;
                float sightMult = isNight ? 2.4f : 0.9f;
                // 출혈 중인 대상은 반경 2배로 멀리서도 냄새로 감지
                if (ncbt && ncbt->isBleeding) sightMult *= 2.0f;
                
                float sightR = (ai.type == ZombieType::Runner)
                               ? ZOMBIE_SIGHT_RADIUS * 1.5f * sightMult
                               : ZOMBIE_SIGHT_RADIUS * sightMult;
                if (dx*dx + dy*dy < sightR * sightR) {
                    // 벽 투시 방지 (Raycast)
                    bool hasLOS = true;
                    if (m_map) {
                        float dist = std::sqrt(dx*dx + dy*dy);
                        int steps = static_cast<int>(dist / 16.0f); // 16픽셀 단위 레이캐스트
                        for (int i = 1; i < steps; ++i) {
                            float t = static_cast<float>(i) / steps;
                            float cx = xf->x + dx * t;
                            float cy = xf->y + dy * t;
                            int tx = TileMap::worldToTile(cx);
                            int ty = TileMap::worldToTile(cy);
                            if (m_map->isSolid(tx, ty)) {
                                hasLOS = false;
                                break;
                            }
                        }
                    }

                    if (hasLOS) {
                        ai.targetX    = nxf->x;
                        ai.targetY    = nxf->y;
                        if (ai.state == ZombieState::Idle || ai.state == ZombieState::Alert) {
                            ai.state      = ZombieState::Chase;
                            ai.stateTimer = 0.0f;
                        }
                    }
                }
            }
        }
    }

    switch (ai.state) {
    // ── Idle ──────────────────────────────────────────────────────────────────
    case ZombieState::Idle:
        ai.alertLevel -= ZOMBIE_ALERT_DECAY * dt;
        if (ai.alertLevel < 0.0f) ai.alertLevel = 0.0f;

        if (!doorTargetActive && maxNoise >= static_cast<uint8_t>(NoiseCategory::Deafening)) {
            ai.targetX = noiseTargetX;
            ai.targetY = noiseTargetY;
            ai.state = ZombieState::Frenzy;
            ai.stateTimer = 0.0f;
            ai.chainTriggered = false;
        } else if (!doorTargetActive && maxNoise >= static_cast<uint8_t>(NoiseCategory::Loud)) {
            ai.targetX = noiseTargetX;
            ai.targetY = noiseTargetY;
            ai.state = ZombieState::Chase;
            ai.stateTimer = 0.0f;
        } else if (!doorTargetActive && maxNoise >= static_cast<uint8_t>(NoiseCategory::Soft)) {
            ai.targetX = noiseTargetX;
            ai.targetY = noiseTargetY;
            if (ai.type == ZombieType::Runner) {
                ai.state = ZombieState::Chase;
                ai.stateTimer = 0.0f;
            } else {
                ai.alertLevel += 0.4f;
                if (ai.alertLevel >= 1.0f) {
                    ai.alertLevel = 1.0f;
                    ai.state      = ZombieState::Alert;
                    ai.stateTimer = 0.0f;
                }
            }
        }
        break;

    // ── Alert ─────────────────────────────────────────────────────────────────
    case ZombieState::Alert:
        ai.alertLevel -= ZOMBIE_ALERT_DECAY * dt * 0.5f; // decay slower in Alert

        if (!doorTargetActive && maxNoise >= static_cast<uint8_t>(NoiseCategory::Deafening)) {
            ai.targetX = noiseTargetX;
            ai.targetY = noiseTargetY;
            ai.state = ZombieState::Frenzy;
            ai.stateTimer = 0.0f;
            ai.chainTriggered = false;
        } else if (!doorTargetActive && maxNoise >= static_cast<uint8_t>(NoiseCategory::Loud)) {
            ai.targetX = noiseTargetX;
            ai.targetY = noiseTargetY;
            ai.state = ZombieState::Chase;
            ai.stateTimer = 0.0f;
        } else if (ai.alertLevel <= 0.0f) {
            ai.alertLevel = 0.0f;
            ai.state = ZombieState::Idle;
            ai.stateTimer = 0.0f;
        }
        break;

    // ── Chase ─────────────────────────────────────────────────────────────────
    case ZombieState::Chase: {
        if (doorTargetActive) {
            ai.stateTimer = 0.0f;
        } else {
            Entity target{NULL_ENTITY};
            if (ai.targetNetID != 0) {
                for (EntityID id : world.alive()) {
                    Entity e{id};
                    auto* net = world.tryGet<NetworkComponent>(e);
                    auto* hp = world.tryGet<HealthComponent>(e);
                    if (net && hp && hp->isAlive && net->netID == ai.targetNetID) {
                        target = e;
                        break;
                    }
                }
            }
            // 매 틱 가장 가까운 적 재탐색 (기존 타겟이 죽었을 때만 변경)
            if (!target.isValid()) target = findNearestEnemy(world, zombie);
            if (target.isValid()) {
                auto* txf  = world.tryGet<TransformComponent>(target);
                auto* tnet = world.tryGet<NetworkComponent>(target);
                if (txf) { ai.targetX = txf->x; ai.targetY = txf->y; }
                if (tnet) ai.targetNetID = tnet->netID;
                ai.stateTimer = 0.0f;
            } else {
                if (ai.stateTimer >= ZOMBIE_SILENCE_CHASE) {
                    ai.state      = ZombieState::Idle;
                    ai.stateTimer = 0.0f;
                    ai.alertLevel = 0.0f;
                }
            }
        }

        if (!doorTargetActive && maxNoise >= static_cast<uint8_t>(NoiseCategory::Deafening)) {
            ai.targetX = noiseTargetX;
            ai.targetY = noiseTargetY;
            ai.state = ZombieState::Frenzy;
            ai.stateTimer = 0.0f;
            ai.chainTriggered = false;
        }
        break;
    }

    // ── Frenzy ────────────────────────────────────────────────────────────────
    case ZombieState::Frenzy:
        // Chain aggro once on entry
        if (!ai.chainTriggered) {
            ai.chainTriggered = true;
            triggerChainFrenzy(world, zombie, xf->x, xf->y);
        }

        // Re-acquire nearest target continuously
        {
            Entity target = findNearestEnemy(world, zombie);
            if (target.isValid()) {
                auto* txf = world.tryGet<TransformComponent>(target);
                if (txf) { ai.targetX = txf->x; ai.targetY = txf->y; }
            }
        }

        if (ai.stateTimer >= ZOMBIE_FRENZY_DURATION) {
            ai.state = ZombieState::Chase;
            ai.stateTimer = 0.0f;
            DZ_LOG_DEBUG("[ZombieAI] %u Frenzy→Chase (timeout)", zombie.id);
        }
        break;

    case ZombieState::Dead:
        break;
    }

    // 밤이 되면 모든 좀비가 가장 가까운 플레이어를 찾아 광분(Frenzy) 모드로 돌입
    if (isNight && (ai.state == ZombieState::Idle || ai.state == ZombieState::Alert)) {
        Entity nearest = findNearestEnemy(world, zombie);
        if (nearest.isValid()) {
            auto* nxf = world.tryGet<TransformComponent>(nearest);
            if (nxf) {
                // 밤: 950픽셀(약 30타일) 내 플레이어 무조건 타겟팅
                float dx = nxf->x - xf->x, dy = nxf->y - xf->y;
                if (dx*dx + dy*dy < 950.0f * 950.0f) {
                    ai.targetX = nxf->x;
                    ai.targetY = nxf->y;
                    ai.state = ZombieState::Frenzy;
                    ai.stateTimer = 0.0f;
                }
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// doMovement — steer toward target / patrol waypoint
// ─────────────────────────────────────────────────────────────────────────────
void ZombieAISystem::doMovement(World& world, Entity zombie,
                                 ZombieAIComponent& ai, float dt) {
    auto* xf = world.tryGet<TransformComponent>(zombie);
    if (!xf) return;

    float speed = ZOMBIE_SPEED_IDLE;
    float destX = ai.patrolWpX, destY = ai.patrolWpY;

    // 타입별 속도 결정
    float chaseSpeed  = (ai.type == ZombieType::Runner) ? RUNNER_SPEED_CHASE
                      : (ai.type == ZombieType::Brute)  ? BRUTE_SPEED_CHASE
                      : ZOMBIE_SPEED_CHASE;
    float frenzySpeed = (ai.type == ZombieType::Runner) ? RUNNER_SPEED_FRENZY
                      : (ai.type == ZombieType::Brute)  ? BRUTE_SPEED_FRENZY
                      : ZOMBIE_SPEED_FRENZY;

    switch (ai.state) {
    case ZombieState::Idle:
        speed = ZOMBIE_SPEED_IDLE;
        ai.patrolTimer -= dt;
        if (ai.patrolTimer <= 0.0f) {
            pickPatrolWaypoint(ai, *xf);
            destX = ai.patrolWpX;
            destY = ai.patrolWpY;
        }
        break;
    case ZombieState::Alert:
        speed  = ZOMBIE_SPEED_ALERT;
        destX  = ai.targetX;
        destY  = ai.targetY;
        break;
    case ZombieState::Chase:
        speed  = chaseSpeed;
        destX  = ai.targetX;
        destY  = ai.targetY;
        break;
    case ZombieState::Frenzy:
        speed  = frenzySpeed;
        destX  = ai.targetX;
        destY  = ai.targetY;
        break;
    default: return;
    }

    float dx = destX - xf->x, dy = destY - xf->y;
    float dist = std::sqrt(dx*dx + dy*dy);
    if (dist < 4.0f) return; // arrived

    float nx = dx / dist, ny = dy / dist;
    // 축 분리 이동 — 좀비도 벽 슬라이딩 warp 방지
    if (m_map) {
        xf->x += nx * speed * dt;
        m_map->resolveAxisX(xf->x, xf->y, 10.0f, 10.0f);
        xf->y += ny * speed * dt;
        m_map->resolveAxisY(xf->x, xf->y, 10.0f, 10.0f);
    } else {
        xf->x += nx * speed * dt;
        xf->y += ny * speed * dt;
    }

    // 좀비끼리 같은 코너/목표점에 겹쳐 쌓이지 않도록 약한 분리 이동.
    float pushX = 0.0f;
    float pushY = 0.0f;
    int pushCount = 0;
    constexpr float SEPARATION_RADIUS = 22.0f;
    constexpr float SEPARATION_R2 = SEPARATION_RADIUS * SEPARATION_RADIUS;
    for (EntityID id : world.alive()) {
        if (id == zombie.id) continue;
        Entity other{id};
        if (!world.tryGet<ZombieAIComponent>(other)) continue;
        auto* ohp = world.tryGet<HealthComponent>(other);
        auto* oxf = world.tryGet<TransformComponent>(other);
        if (!ohp || !ohp->isAlive || !oxf) continue;
        float odx = xf->x - oxf->x;
        float ody = xf->y - oxf->y;
        float d2 = odx * odx + ody * ody;
        if (d2 <= 0.01f || d2 > SEPARATION_R2) continue;
        float invD = 1.0f / std::sqrt(d2);
        float strength = 1.0f - (std::sqrt(d2) / SEPARATION_RADIUS);
        pushX += odx * invD * strength;
        pushY += ody * invD * strength;
        ++pushCount;
    }
    if (pushCount > 0) {
        float pushLen = std::sqrt(pushX * pushX + pushY * pushY);
        if (pushLen > 0.01f) {
            constexpr float SEPARATION_SPEED = 56.0f;
            pushX = (pushX / pushLen) * SEPARATION_SPEED * dt;
            pushY = (pushY / pushLen) * SEPARATION_SPEED * dt;
            if (m_map) {
                xf->x += pushX;
                m_map->resolveAxisX(xf->x, xf->y, 10.0f, 10.0f);
                xf->y += pushY;
                m_map->resolveAxisY(xf->x, xf->y, 10.0f, 10.0f);
            } else {
                xf->x += pushX;
                xf->y += pushY;
            }
        }
    }

    xf->rotation = std::atan2(nx, -ny) * (180.0f / 3.14159265f);
    if (xf->rotation < 0.0f) xf->rotation += 360.0f;
}

// ─────────────────────────────────────────────────────────────────────────────
// doAttack — deal melee damage to entities within attack radius
// ─────────────────────────────────────────────────────────────────────────────
void ZombieAISystem::doAttack(World& world, Entity zombie,
                               ZombieAIComponent& ai, float dt) {
    if (ai.state == ZombieState::Idle || ai.state == ZombieState::Dead) return;

    ai.attackTimer -= dt;
    if (ai.attackTimer > 0.0f) return;

    auto* xf = world.tryGet<TransformComponent>(zombie);
    if (!xf) return;

    for (EntityID id : world.alive()) {
        Entity e{id};
        if (e.id == zombie.id) continue;
        auto* txf = world.tryGet<TransformComponent>(e);
        auto* thp = world.tryGet<HealthComponent>(e);
        if (!txf || !thp || !thp->isAlive) continue;
        if (thp->team == Team::Neutral) continue; // don't attack other zombies

        // 타입별 공격력 & 쿨다운
        float dmg      = (ai.type == ZombieType::Runner) ? RUNNER_ATTACK_DAMAGE
                        : (ai.type == ZombieType::Brute) ? BRUTE_ATTACK_DAMAGE
                        : ZOMBIE_ATTACK_DAMAGE;
        float atkRate  = (ai.type == ZombieType::Runner) ? RUNNER_ATTACK_RATE
                        : (ai.type == ZombieType::Brute) ? BRUTE_ATTACK_RATE
                        : ZOMBIE_ATTACK_RATE;
        float atkR     = (ai.type == ZombieType::Brute)  ? BRUTE_ATTACK_RADIUS
                        : ZOMBIE_ATTACK_RADIUS;

        float dx2 = txf->x - xf->x, dy2 = txf->y - xf->y;
        if (dx2*dx2 + dy2*dy2 > atkR * atkR) continue;
        if (m_map) {
            float dist = std::sqrt(dx2*dx2 + dy2*dy2);
            int steps = static_cast<int>(dist / 8.0f);
            bool blocked = false;
            for (int i = 1; i <= steps; ++i) {
                float t = static_cast<float>(i) / std::max(1, steps);
                float cx = xf->x + dx2 * t;
                float cy = xf->y + dy2 * t;
                if (m_map->isSolid(TileMap::worldToTile(cx), TileMap::worldToTile(cy))) {
                    blocked = true;
                    break;
                }
            }
            if (blocked) continue;
        }

        if (ai.state == ZombieState::Frenzy) dmg *= 1.4f;

        // CombatSystem을 통해 데미지 적용 → onDamage 콜백 발동 → 클라이언트 HP 동기화
        if (m_combat) {
            auto result = m_combat->applyDamage(world, e, zombie, dmg, DamageType::Zombie);
            // 출혈 적용
            auto* cbt = world.tryGet<CombatComponent>(e);
            if (cbt && !cbt->isBleeding && ai.type == ZombieType::Brute) {
                float roll = static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
                float bleedChance = 0.20f;
                if (roll < bleedChance) cbt->applyBleed(4.0f, 6.0f);
            }
            (void)result;
        } else {
            // fallback (CombatSystem 없을 경우)
            thp->applyDamage(dmg, DamageType::Zombie);
            auto* vnet = world.tryGet<NetworkComponent>(e);
            if (vnet) vnet->markDirty(DIRTY_HEALTH);
        }

        ai.attackTimer = atkRate;
        break;
    }

    // 플레이어를 못 찾았으면 인접한 건물(바리케이드/포탑) 공격
    if (ai.attackTimer <= 0.0f) {
        float atkR = (ai.type == ZombieType::Brute) ? BRUTE_ATTACK_RADIUS : ZOMBIE_ATTACK_RADIUS;
        float dmg  = (ai.type == ZombieType::Brute) ? BRUTE_ATTACK_DAMAGE * 0.6f
                                                     : ZOMBIE_ATTACK_DAMAGE * 0.5f;
        for (EntityID bid : world.alive()) {
            Entity be{bid};
            auto* bld = world.tryGet<BuildingComponent>(be);
            if (!bld || bld->isDestroyed) continue;
            auto* bxf = world.tryGet<TransformComponent>(be);
            auto* bhp = world.tryGet<HealthComponent>(be);
            if (!bxf || !bhp || !bhp->isAlive) continue;
            float dx2 = bxf->x - xf->x, dy2 = bxf->y - xf->y;
            if (dx2*dx2 + dy2*dy2 > atkR * atkR) continue;
            if (m_combat) {
                m_combat->applyDamage(world, be, zombie, dmg, DamageType::Melee);
            } else {
                bhp->applyDamage(dmg, DamageType::Melee);
                auto* bnet = world.tryGet<NetworkComponent>(be);
                if (bnet) bnet->markDirty(DIRTY_HEALTH);
            }
            ai.attackTimer = ZOMBIE_ATTACK_RATE;
            break;
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// pickPatrolWaypoint — random wander
// ─────────────────────────────────────────────────────────────────────────────
void ZombieAISystem::pickPatrolWaypoint(ZombieAIComponent& ai,
                                         const TransformComponent& xf) noexcept {
    if (m_map) {
        const int ztx = TileMap::worldToTile(xf.x);
        const int zty = TileMap::worldToTile(xf.y);
        for (const auto& b : m_map->getBuildings()) {
            if (ztx < b.x || ztx >= b.x + b.w || zty < b.y || zty >= b.y + b.h) continue;

            const int insetX = (b.w > 6) ? 2 : 1;
            const int insetY = (b.h > 6) ? 2 : 1;
            const int usableW = std::max(1, b.w - insetX * 2);
            const int usableH = std::max(1, b.h - insetY * 2);
            for (int attempt = 0; attempt < 8; ++attempt) {
                int tx = b.x + insetX + (std::rand() % usableW);
                int ty = b.y + insetY + (std::rand() % usableH);
                if (m_map->isSolid(tx, ty)) continue;
                ai.patrolWpX = TileMap::tileCentre(tx);
                ai.patrolWpY = TileMap::tileCentre(ty);
                ai.patrolTimer = ZOMBIE_PATROL_INTERVAL;
                return;
            }
            ai.patrolWpX = xf.x;
            ai.patrolWpY = xf.y;
            ai.patrolTimer = ZOMBIE_PATROL_INTERVAL;
            return;
        }
    }

    float angle  = static_cast<float>(std::rand() % 360) * (3.14159265f / 180.0f);
    float radius = 64.0f + static_cast<float>(std::rand() % 128);
    ai.patrolWpX   = xf.x + std::cos(angle) * radius;
    ai.patrolWpY   = xf.y + std::sin(angle) * radius;
    if (m_map && m_map->isSolid(TileMap::worldToTile(ai.patrolWpX),
                                TileMap::worldToTile(ai.patrolWpY))) {
        ai.patrolWpX = xf.x;
        ai.patrolWpY = xf.y;
    }
    ai.patrolTimer = ZOMBIE_PATROL_INTERVAL;
}

// ─────────────────────────────────────────────────────────────────────────────
// triggerChainFrenzy — transition all nearby zombies to Frenzy
// ─────────────────────────────────────────────────────────────────────────────
void ZombieAISystem::triggerChainFrenzy(World& world, Entity source,
                                         float wx, float wy) {
    auto& aiPool = world.pool<ZombieAIComponent>();
    auto& xfPool = world.pool<TransformComponent>();

    int chainCount = 0;
    for (size_t i = 0; i < aiPool.owners().size(); ++i) {
        Entity z{aiPool.owners()[i]};
        if (z.id == source.id) continue;

        auto* zxf = xfPool.get(z.id);
        if (!zxf) continue;
        float dx = zxf->x - wx, dy = zxf->y - wy;
        if (dx*dx + dy*dy > ZOMBIE_FRENZY_CHAIN_R * ZOMBIE_FRENZY_CHAIN_R) continue;

        auto& zai = aiPool.data()[i];
        if (zai.state != ZombieState::Frenzy && zai.state != ZombieState::Dead) {
            zai.state         = ZombieState::Frenzy;
            zai.stateTimer    = 0.0f;
            zai.chainTriggered = true; // don't recurse further
            ++chainCount;
        }
    }
    if (chainCount > 0)
        DZ_LOG_INFO("[ZombieAI] Chain frenzy: %d zombies activated", chainCount);
}

// ─────────────────────────────────────────────────────────────────────────────
// findNearestEnemy — returns closest alive non-zombie entity
// ─────────────────────────────────────────────────────────────────────────────
Entity ZombieAISystem::findNearestEnemy(World& world, Entity zombie) {
    auto* zxf = world.tryGet<TransformComponent>(zombie);
    if (!zxf) return Entity{NULL_ENTITY};

    Entity nearest{NULL_ENTITY};
    float  nearDist = 1e9f;

    for (EntityID id : world.alive()) {
        Entity e{id};
        if (e.id == zombie.id) continue;
        auto* hp  = world.tryGet<HealthComponent>(e);
        auto* xf  = world.tryGet<TransformComponent>(e);
        if (!hp || !xf || !hp->isAlive) continue;
        if (hp->team == Team::Neutral) continue; // skip other zombies

        float dx = xf->x - zxf->x, dy = xf->y - zxf->y;
        float d  = std::sqrt(dx*dx + dy*dy);
        if (d < nearDist && d < 960.0f) {
            bool hitWall = false;
            if (m_map) {
                int steps = static_cast<int>(d / 16.0f); // Check every 16 pixels (half tile)
                for (int i = 1; i <= steps; ++i) {
                    float t = static_cast<float>(i) / std::max(1, steps);
                    float cx = zxf->x + dx * t;
                    float cy = zxf->y + dy * t;
                    if (m_map->isSolid(TileMap::worldToTile(cx), TileMap::worldToTile(cy))) {
                        hitWall = true;
                        break;
                    }
                }
            }
            if (!hitWall) {
                nearDist = d; 
                nearest = e;
            }
        }
    }
    return nearest;
}

} // namespace dz
