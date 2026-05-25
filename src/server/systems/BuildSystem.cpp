#include "BuildSystem.h"
#include "shared/ecs/components/TransformComponent.h"
#include "shared/ecs/components/HealthComponent.h"
#include "shared/ecs/components/CollisionComponent.h"
#include "shared/ecs/components/NetworkComponent.h"
#include "shared/ecs/components/InventoryComponent.h"
#include "shared/ecs/components/CombatComponent.h"
#include "shared/util/Logger.h"
#include <cmath>

namespace dz {

constexpr float BUILD_RANGE = 96.0f; // px — player must be within 3 tiles

Entity BuildSystem::tryBuild(World& world, TileMap& map,
                              uint32_t ownerTeam,
                              Entity   requester,
                              int tileX, int tileY,
                              BuildingType type) {
    // ── Range check ───────────────────────────────────────────────────────────
    auto* xf = world.tryGet<TransformComponent>(requester);
    if (!xf) return Entity{NULL_ENTITY};

    float tx = TileMap::tileCentre(tileX);
    float ty = TileMap::tileCentre(tileY);
    float dx = tx - xf->x, dy = ty - xf->y;
    if (std::sqrt(dx*dx + dy*dy) > BUILD_RANGE) {
        DZ_LOG_DEBUG("[Build] Out of range (%.0f px)", std::sqrt(dx*dx+dy*dy));
        return Entity{NULL_ENTITY};
    }

    // ── Tile availability ─────────────────────────────────────────────────────
    if (!map.inBounds(tileX, tileY) || map.at(tileX, tileY).isOccupied()) {
        DZ_LOG_DEBUG("[Build] Tile (%d,%d) occupied or OOB", tileX, tileY);
        return Entity{NULL_ENTITY};
    }

    // ── Material check ────────────────────────────────────────────────────────
    auto* inv = world.tryGet<InventoryComponent>(requester);
    const char* matKey = nullptr;
    int requiredQty = 1;
    if (type == BuildingType::Barricade) { matKey = "barricade_item"; }
    else if (type == BuildingType::Turret) { matKey = "turret_item"; }
    else if (type == BuildingType::Workbench) { matKey = "wood_plank"; requiredQty = 2; }

    if (inv && matKey) {
        bool found = false;
        for (int i = 0; i < INVENTORY_GRID_SLOTS; ++i) {
            if (inv->slots[i].isValid() && inv->slots[i].key == matKey && inv->slots[i].quantity >= requiredQty) {
                inv->slots[i].quantity -= requiredQty;
                if (inv->slots[i].quantity <= 0)
                    inv->removeItem(i);
                found = true;
                break;
            }
        }
        if (!found) {
            DZ_LOG_DEBUG("[Build] Not enough material '%s' (need %d) in inventory", matKey, requiredQty);
            return Entity{NULL_ENTITY};
        }
    }

    // ── Spawn building entity ─────────────────────────────────────────────────
    Entity e = world.createEntity();

    auto& bld         = world.addComponent<BuildingComponent>(e);
    bld.type          = type;
    bld.ownerTeam     = static_cast<uint8_t>(ownerTeam);
    bld.tileX         = tileX;
    bld.tileY         = tileY;
    bld.maxHp         = (type == BuildingType::Workbench) ? 300.0f : (type == BuildingType::Barricade) ? 150.0f : 250.0f;
    bld.currentHp     = bld.maxHp;

    auto& xfComp      = world.addComponent<TransformComponent>(e);
    xfComp.x          = tx;
    xfComp.y          = ty;

    auto& hp          = world.addComponent<HealthComponent>(e);
    hp.maxHp          = bld.maxHp;
    hp.currentHp      = bld.currentHp;
    hp.team           = static_cast<Team>(ownerTeam);
    hp.isAlive        = true;

    auto& col         = world.addComponent<CollisionComponent>(e);
    col.width         = static_cast<float>(TILE_SIZE - 2);
    col.height        = static_cast<float>(TILE_SIZE - 2);
    col.isSolid       = true;
    col.isStatic      = true;
    col.layer         = COL_BUILDING;
    col.maskLayer     = COL_PLAYER | COL_ZOMBIE | COL_PROJECTILE;

    auto& net         = world.addComponent<NetworkComponent>(e);
    net.netID         = m_nextNetID++;
    net.role          = NetRole::ServerAuth;

    // Reserve tile in map
    map.setOccupied(tileX, tileY, e.id, true);

    // If barricade, mark tile as flammable
    if (type == BuildingType::Barricade) {
        map.at(tileX, tileY).flags |= TILE_FLAMMABLE;
    }

    DZ_LOG_INFO("[Build] %s spawned at tile (%d,%d) netID=%u",
        type == BuildingType::Barricade ? "Barricade" : type == BuildingType::Turret ? "Turret" : "Workbench",
        tileX, tileY, net.netID);

    net.markDirty(DIRTY_TRANSFORM);
    if (m_onSpawn) m_onSpawn(e);

    // Emit build noise
    auto* cbt = world.tryGet<CombatComponent>(requester);
    if (cbt) cbt->emitNoise(300.0f, 3); // Loud

    return e;
}

void BuildSystem::destroyBuilding(World& world, TileMap& map, Entity building) {
    auto* bld = world.tryGet<BuildingComponent>(building);
    if (!bld || bld->isDestroyed) return;
    bld->isDestroyed = true;
    map.clearOccupied(bld->tileX, bld->tileY);
    world.destroyEntity(building);
}

void BuildSystem::updateTurrets(World& world, float dt) {
    auto& bldPool = world.pool<BuildingComponent>();
    auto& xfPool  = world.pool<TransformComponent>();

    for (size_t i = 0; i < bldPool.owners().size(); ++i) {
        auto& bld = bldPool.data()[i];
        if (bld.isDestroyed || bld.type != BuildingType::Turret) continue;

        Entity turret{bldPool.owners()[i]};
        auto* xf = xfPool.get(turret.id);
        if (!xf) continue;

        // 쿨다운 감소
        bld.turretCooldown -= dt;
        if (bld.turretCooldown > 0.0f) continue;

        // ── 사거리 내 가장 가까운 적 탐색 ─────────────────────────────────────
        float    nearDist = bld.turretRange;
        Entity   target{NULL_ENTITY};
        float    targetX = 0.0f, targetY = 0.0f;

        for (EntityID id : world.alive()) {
            Entity e{id};
            if (e.id == turret.id) continue;

            auto* thp = world.tryGet<HealthComponent>(e);
            auto* txf = world.tryGet<TransformComponent>(e);
            if (!thp || !txf || !thp->isAlive) continue;

            // ── 팀 체크: 같은 팀 유닛은 공격하지 않음 ─────────────────────────
            // 포탑 ownerTeam == 0 이면 neutral (좀비 전용)
            // 플레이어/건물은 Team enum, 좀비는 Team::None
            Team targetTeam = thp->team;
            if (bld.ownerTeam != 0) {
                // 팀이 있는 포탑: 같은 팀 엔티티는 스킵
                if (static_cast<uint8_t>(targetTeam) == bld.ownerTeam) continue;
            }

            // 건물 자체는 표적으로 삼지 않음 (BuildingComponent 있으면 스킵)
            if (world.tryGet<BuildingComponent>(e)) continue;

            float dx = txf->x - xf->x, dy = txf->y - xf->y;
            float d  = std::sqrt(dx*dx + dy*dy);
            if (d < nearDist) {
                nearDist = d;
                target   = e;
                targetX  = txf->x;
                targetY  = txf->y;
            }
        }

        if (!target.isValid()) continue;

        // ── 데미지 적용 ──────────────────────────────────────────────────────
        auto* thp = world.tryGet<HealthComponent>(target);
        if (thp) {
            thp->applyDamage(bld.turretDamage, DamageType::Bullet);
            auto* tnet = world.tryGet<NetworkComponent>(target);
            if (tnet) tnet->markDirty(DIRTY_HEALTH);
        }

        // ── 쿨다운 리셋 ──────────────────────────────────────────────────────
        bld.turretCooldown = 1.0f / bld.turretFireRate;

        // ── 발사 이벤트 콜백 (클라이언트 레이저 시각화용) ────────────────────
        if (m_onTurretFire) {
            auto* bnet = world.tryGet<NetworkComponent>(turret);
            uint16_t turretNetID = bnet ? static_cast<uint16_t>(bnet->netID) : 0;
            m_onTurretFire(turretNetID,
                           xf->x, xf->y,
                           targetX, targetY,
                           bld.ownerTeam);
        }

        DZ_LOG_DEBUG("[Turret] netID=%u fired at (%.0f,%.0f) dmg=%.0f",
            world.tryGet<NetworkComponent>(turret)
                ? world.tryGet<NetworkComponent>(turret)->netID : 0,
            targetX, targetY, bld.turretDamage);
    }
}


} // namespace dz
