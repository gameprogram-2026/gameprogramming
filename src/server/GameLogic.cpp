#include "GameLogic.h"
#include "shared/ecs/components/TransformComponent.h"
#include "shared/ecs/components/HealthComponent.h"
#include "shared/ecs/components/NetworkComponent.h"
#include "shared/ecs/components/CombatComponent.h"
#include "shared/ecs/components/InventoryComponent.h"
#include "shared/ItemData.h"
#include "shared/util/Logger.h"
#include <cmath>

namespace dz {

void GameLogic::processInput(uint32_t ownerID, const InputPacket& pkt) {
    if (pkt.actions & ACT_SHOOT) {
        handleRangedFire(ownerID, pkt.aimAngle);
    }
    if (pkt.actions & ACT_MELEE) {
        handleMeleeAttack(ownerID);
    }
    if (pkt.actions & ACT_RELOAD) {
        handleReload(ownerID);
    }
}

void GameLogic::handleBuildRequest(uint32_t ownerID,
                                    int tileX, int tileY,
                                    BuildingType type) {
    Entity e = findOwnedEntity(ownerID);
    if (!e.isValid()) return;

    auto* hp = m_world.tryGet<HealthComponent>(e);
    if (!hp || !hp->isAlive) return;

    Entity building = m_build.tryBuild(m_world, m_map,
                                        static_cast<uint32_t>(hp->team),
                                        e, tileX, tileY, type);
    if (!building.isValid()) {
        DZ_LOG_DEBUG("[Logic] Build failed for owner %u at tile (%d,%d)",
                     ownerID, tileX, tileY);
    }
}

void GameLogic::handleFireThrow(uint32_t ownerID,
                                 float originX, float originY) {
    Entity e = findOwnedEntity(ownerID);
    if (!e.isValid()) return;

    auto* hp = m_world.tryGet<HealthComponent>(e);
    if (!hp || !hp->isAlive) return;

    auto* inv = m_world.tryGet<InventoryComponent>(e);
    if (inv) {
        for (int i = 0; i < INVENTORY_GRID_SLOTS; ++i) {
            if (inv->slots[i].isValid() && inv->slots[i].key == "molotov") {
                inv->removeItem(i);
                break;
            }
        }
    }

    m_fire.igniteAtWorld(originX, originY);

    auto* cbt = m_world.tryGet<CombatComponent>(e);
    if (cbt) cbt->emitNoise(NOISE_PISTOL_RADIUS, 3);
    DZ_LOG_INFO("[Logic] Molotov thrown by %u at (%.0f, %.0f)", ownerID, originX, originY);
}

void GameLogic::handleMeleeAttack(uint32_t ownerID) {
    Entity e = findOwnedEntity(ownerID);
    if (!e.isValid()) return;
    m_combat.tryMeleeAttack(m_world, e);
}

// ─────────────────────────────────────────────────────────────────────────────
// handleRangedFire — BFS-style raycast bullet in aimAngle direction
// ─────────────────────────────────────────────────────────────────────────────
void GameLogic::handleRangedFire(uint32_t ownerID, float aimAngle) {
    Entity e = findOwnedEntity(ownerID);
    if (!e.isValid()) return;

    auto* hp  = m_world.tryGet<HealthComponent>(e);
    auto* xf  = m_world.tryGet<TransformComponent>(e);
    auto* cbt = m_world.tryGet<CombatComponent>(e);
    if (!hp || !hp->isAlive || !xf || !cbt) return;
    if (cbt->fireCooldown > 0.0f || cbt->isReloading) return;
    if (cbt->ammoInMag <= 0) return;

    // Determine damage and noise from equipped weapon
    float damage  = 60.0f;
    float noiseR  = NOISE_PISTOL_RADIUS;
    auto* inv = m_world.tryGet<InventoryComponent>(e);
    if (inv) {
        const Item& w = inv->activeWeapon();
        if (w.isValid()) {
            if (w.key == "pistol_9mm")   { damage = 60.0f; noiseR = NOISE_PISTOL_RADIUS; }
            // Flamethrower handled separately as area fire — skip here
        }
    }

    --cbt->ammoInMag;
    cbt->fireCooldown = cbt->fireRate;
    cbt->emitNoise(noiseR, 3); // Loud

    // 15% chance to trigger massive zombie wave penalty removed for better gameplay

    // Raycast: step along aim direction, stop at solid tile or first entity hit
    constexpr float PI    = 3.14159265f;
    float rad  = aimAngle * (PI / 180.0f);
    float dirX =  std::sin(rad);
    float dirY = -std::cos(rad);

    constexpr float STEP    = 6.0f;
    constexpr float MAX_R   = 1280.0f; // 40 tiles
    constexpr float HIT_R2  = 16.0f * 16.0f;

    for (float t = STEP; t <= MAX_R; t += STEP) {
        float bx = xf->x + dirX * t;
        float by = xf->y + dirY * t;

        for (EntityID tid : m_world.alive()) {
            Entity target{tid};
            if (target == e) continue;
            auto* txf = m_world.tryGet<TransformComponent>(target);
            auto* thp = m_world.tryGet<HealthComponent>(target);
            if (!txf || !thp || !thp->isAlive) continue;
            float dx = txf->x - bx, dy = txf->y - by;
            if (dx*dx + dy*dy < HIT_R2) {
                m_combat.applyDamage(m_world, target, e, damage, DamageType::Bullet);
                DZ_LOG_DEBUG("[Logic] Bullet hit entity %u for %.0f dmg", tid, damage);
                
                auto* net = m_world.tryGet<NetworkComponent>(e);
                if (m_onRangedFire && net) {
                    m_onRangedFire(net->netID, xf->x, xf->y, bx, by, static_cast<uint8_t>(hp->team));
                }
                goto done;
            }
        }

        if (m_map.isSolid(TileMap::worldToTile(bx), TileMap::worldToTile(by))) {
            auto* net = m_world.tryGet<NetworkComponent>(e);
            if (m_onRangedFire && net) {
                m_onRangedFire(net->netID, xf->x, xf->y, bx, by, static_cast<uint8_t>(hp->team));
            }
            break;
        }
    }
    
    // 만약 허공(최대 사거리)까지 맞은 게 없다면 끝 위치로 이펙트 전송
    {
        auto* net = m_world.tryGet<NetworkComponent>(e);
        if (m_onRangedFire && net) {
            float endX = xf->x + dirX * MAX_R;
            float endY = xf->y + dirY * MAX_R;
            m_onRangedFire(net->netID, xf->x, xf->y, endX, endY, static_cast<uint8_t>(hp->team));
        }
    }
    
    done:;
}

void GameLogic::handleReload(uint32_t ownerID) {
    Entity e = findOwnedEntity(ownerID);
    if (!e.isValid()) return;
    auto* cbt = m_world.tryGet<CombatComponent>(e);
    if (!cbt || cbt->isReloading) return;
    if (cbt->ammoInMag >= cbt->magCapacity) return;
    if (cbt->ammoReserve <= 0) return;

    cbt->isReloading = true;
    cbt->reloadTimer = cbt->reloadTime;
    DZ_LOG_DEBUG("[Logic] Reload started for owner %u", ownerID);
}

void GameLogic::handleAlliancePropose(uint8_t fromTeam, uint8_t toTeam) {
    bool established = m_alliance.proposeAlliance(fromTeam, toTeam);
    DZ_LOG_INFO("[Logic] Alliance propose %u→%u: %s",
                fromTeam, toTeam, established ? "ESTABLISHED" : "pending");
}

void GameLogic::handleAllianceBreak(uint8_t fromTeam, uint8_t toTeam) {
    m_alliance.breakAlliance(fromTeam, toTeam);
}

// ─────────────────────────────────────────────────────────────────────────────
// handleLootPickup — range check, transfer item, destroy loot entity
// ─────────────────────────────────────────────────────────────────────────────
void GameLogic::handleLootPickup(uint32_t ownerID, uint32_t lootNetID) {
    Entity e = findOwnedEntity(ownerID);
    if (!e.isValid()) return;

    auto* xf  = m_world.tryGet<TransformComponent>(e);
    auto* inv = m_world.tryGet<InventoryComponent>(e);
    if (!xf || !inv) return;

    // Find loot entity by netID
    Entity loot{NULL_ENTITY};
    for (EntityID id : m_world.alive()) {
        Entity le{id};
        auto* net = m_world.tryGet<NetworkComponent>(le);
        if (net && net->netID == lootNetID) { loot = le; break; }
    }
    if (!loot.isValid()) return;

    auto* lxf  = m_world.tryGet<TransformComponent>(loot);
    if (!lxf) return;

    // Max 3-tile pickup range
    float dx = lxf->x - xf->x, dy = lxf->y - xf->y;
    if (dx*dx + dy*dy > 96.0f * 96.0f) return;

    // Transfer all items from loot entity inventory into player inventory
    auto* linv = m_world.tryGet<InventoryComponent>(loot);
    if (linv) {
        for (int i = 0; i < INVENTORY_GRID_SLOTS; ++i) {
            if (!linv->slots[i].isValid()) continue;
            if (inv->addItem(linv->slots[i]))
                linv->removeItem(i);
        }
    }

    m_world.destroyEntity(loot);
    DZ_LOG_INFO("[Logic] Loot %u picked up by owner %u", lootNetID, ownerID);
}

// ─────────────────────────────────────────────────────────────────────────────
// handleCraftRequest — ItemData.h 레시피 테이블 기반 범용 조합 처리
// ─────────────────────────────────────────────────────────────────────────────
void GameLogic::handleCraftRequest(uint32_t ownerID, uint8_t recipeID) {
    Entity e = findOwnedEntity(ownerID);
    if (!e.isValid()) return;

    auto* xf  = m_world.tryGet<TransformComponent>(e);
    auto* inv = m_world.tryGet<InventoryComponent>(e);
    if (!xf || !inv) return;

    // recipeID 유효성 검사
    if (recipeID >= CRAFT_RECIPE_COUNT) {
        DZ_LOG_WARN("[Craft] Invalid recipeID %u from owner %u", recipeID, ownerID);
        return;
    }
    const CraftingRecipe& rec = CRAFT_RECIPES[recipeID];

    // 워크벤치 필요 여부 확인
    if (rec.requiresWorkbench) {
        bool nearWorkbench = false;
        auto& bldPool = m_world.pool<BuildingComponent>();
        auto& xfPool  = m_world.pool<TransformComponent>();
        for (size_t i = 0; i < bldPool.owners().size(); ++i) {
            auto& bld = bldPool.data()[i];
            if (bld.isDestroyed || !bld.isWorkbench()) continue;
            auto* wbxf = xfPool.get(bldPool.owners()[i]);
            if (!wbxf) continue;
            float dx = wbxf->x - xf->x, dy = wbxf->y - xf->y;
            if (dx*dx + dy*dy <= 96.0f * 96.0f) { nearWorkbench = true; break; }
        }
        if (!nearWorkbench) {
            DZ_LOG_DEBUG("[Craft] %u: recipe %u requires workbench nearby", ownerID, recipeID);
            return;
        }
    }

    // 재료 보유량 확인
    for (int ii = 0; ii < rec.ingredientCount; ++ii) {
        int have = 0;
        for (int si = 0; si < INVENTORY_GRID_SLOTS; ++si) {
            if (inv->slots[si].isValid() && inv->slots[si].key == rec.ingredients[ii].key)
                have += inv->slots[si].quantity;
        }
        if (have < rec.ingredients[ii].qty) {
            DZ_LOG_DEBUG("[Craft] %u: insufficient %s (%d/%d)",
                         ownerID, rec.ingredients[ii].key, have, rec.ingredients[ii].qty);
            return;
        }
    }

    // 인벤토리 여유 공간 확인
    if (inv->isFull()) {
        DZ_LOG_DEBUG("[Craft] %u: inventory full", ownerID);
        return;
    }

    // 재료 소비
    for (int ii = 0; ii < rec.ingredientCount; ++ii) {
        int toRemove = rec.ingredients[ii].qty;
        for (int si = 0; si < INVENTORY_GRID_SLOTS && toRemove > 0; ++si) {
            if (!inv->slots[si].isValid() || inv->slots[si].key != rec.ingredients[ii].key) continue;
            int take = std::min(toRemove, inv->slots[si].quantity);
            inv->slots[si].quantity -= take;
            toRemove -= take;
            if (inv->slots[si].quantity <= 0) inv->removeItem(si);
        }
    }

    // 결과물 생성
    const ItemMeta* meta = findItemMeta(rec.resultKey);
    Item result;
    result.itemID   = 90 + recipeID; // 가상 ID (90번대)
    result.key      = rec.resultKey;
    result.category = ItemCategory::BuildMaterial;
    result.quantity = rec.resultQty;
    result.weight   = 5.0f; // 기본 무게 — 향후 ItemMeta에 추가 가능
    inv->addItem(result);

    DZ_LOG_INFO("[Craft] owner=%u recipe=%u → %s x%d",
                ownerID, recipeID, rec.resultKey, rec.resultQty);
}

// ─────────────────────────────────────────────────────────────────────────────
// handleUseItem — 소모품 사용: 인벤토리에서 제거 후 HP/출혈 효과 적용
// ─────────────────────────────────────────────────────────────────────────────
void GameLogic::handleUseItem(uint32_t ownerID, const char* key) {
    Entity e = findOwnedEntity(ownerID);
    if (!e.isValid()) return;

    auto* hp  = m_world.tryGet<HealthComponent>(e);
    auto* inv = m_world.tryGet<InventoryComponent>(e);
    auto* cbt = m_world.tryGet<CombatComponent>(e);
    if (!hp || !inv || !hp->isAlive) return;

    // 인벤토리에서 해당 키 아이템 찾아 제거
    bool found = false;
    for (int i = 0; i < INVENTORY_GRID_SLOTS; ++i) {
        if (!inv->slots[i].isValid()) continue;
        if (inv->slots[i].key != key) continue;
        inv->removeItem(i);
        found = true;
        break;
    }
    if (!found) return;

    // 아이템 효과 적용
    std::string k(key);
    if (k == "medkit") {
        hp->heal(50.0f);
        if (cbt) cbt->cureBleed();
        DZ_LOG_INFO("[Item] Player %u used medkit → HP %.0f", ownerID, hp->currentHp);
    } else if (k == "bandage") {
        hp->heal(20.0f);
        if (cbt) cbt->cureBleed();
        DZ_LOG_INFO("[Item] Player %u used bandage → HP %.0f", ownerID, hp->currentHp);
    } else if (k == "food_can") {
        hp->heal(10.0f);
        DZ_LOG_INFO("[Item] Player %u used food_can → HP %.0f", ownerID, hp->currentHp);
    }
    // 필요시 더 추가 (예: rare_meds → heal 80)
}

Entity GameLogic::findOwnedEntity(uint32_t ownerID) {
    for (EntityID id : m_world.alive()) {
        Entity e{id};
        auto* net = m_world.tryGet<NetworkComponent>(e);
        if (net && net->ownerID == ownerID) return e;
    }
    return Entity{NULL_ENTITY};
}

} // namespace dz
