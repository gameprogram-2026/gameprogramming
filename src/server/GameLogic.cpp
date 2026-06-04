#include "GameLogic.h"
#include "shared/ecs/components/TransformComponent.h"
#include "shared/ecs/components/HealthComponent.h"
#include "shared/ecs/components/NetworkComponent.h"
#include "shared/ecs/components/CombatComponent.h"
#include "shared/ecs/components/InventoryComponent.h"
#include "shared/ItemData.h"
#include "shared/util/Logger.h"
#include <algorithm>
#include <cmath>
#include <string>

namespace dz {

namespace {

struct ItemDef {
    uint32_t id;
    ItemCategory category;
    float weight;
};

ItemDef itemDefForKey(const std::string& key) {
    if (key == "scrap_pipe")      return {1,  ItemCategory::Weapon,        1.5f};
    if (key == "nail_bat")        return {2,  ItemCategory::Weapon,        2.0f};
    if (key == "fire_axe")        return {3,  ItemCategory::Weapon,        3.0f};
    if (key == "pistol_9mm")      return {4,  ItemCategory::Weapon,        1.0f};
    if (key == "molotov")         return {5,  ItemCategory::Throwable,     0.5f};
    if (key == "flamethrower")    return {6,  ItemCategory::Weapon,        5.0f};
    if (key == "ammo_9mm")        return {10, ItemCategory::Ammo,          0.3f};
    if (key == "scrap_metal")     return {20, ItemCategory::BuildMaterial, 1.0f};
    if (key == "plank")           return {21, ItemCategory::BuildMaterial, 0.8f};
    if (key == "electronic_part") return {22, ItemCategory::BuildMaterial, 0.5f};
    if (key == "oil")             return {23, ItemCategory::BuildMaterial, 1.2f};
    if (key == "wood")            return {24, ItemCategory::BuildMaterial, 0.7f};
    if (key == "medkit")          return {30, ItemCategory::Consumable,    1.0f};
    if (key == "bandage")         return {31, ItemCategory::Consumable,    0.3f};
    if (key == "food_can")        return {32, ItemCategory::Consumable,    0.4f};
    return {0, ItemCategory::Misc, 1.0f};
}

} // namespace

void GameLogic::processInput(uint32_t ownerID, const InputPacket& pkt) {
    if (pkt.actions & (ACT_SHOOT | ACT_MELEE)) {
        Entity e = findOwnedEntity(ownerID);
        auto* inv = e.isValid() ? m_world.tryGet<InventoryComponent>(e) : nullptr;
        const Item* weapon = (inv && inv->activeWeapon().isValid()) ? &inv->activeWeapon() : nullptr;

        if (weapon && weapon->key == "pistol_9mm") {
            handleRangedFire(ownerID, pkt.aimAngle);
        } else if (weapon && weapon->key == "flamethrower") {
            handleFlamethrowerBurst(ownerID, pkt.aimAngle);
        } else if (weapon && weapon->category == ItemCategory::Weapon) {
            handleMeleeAttack(ownerID);
        }
    }
    if (pkt.actions & ACT_RELOAD) {
        handleReload(ownerID);
    }
}

bool GameLogic::handleBuildRequest(uint32_t ownerID,
                                    int tileX, int tileY,
                                    BuildingType type,
                                    uint8_t direction) {
    Entity e = findOwnedEntity(ownerID);
    if (!e.isValid()) return false;

    auto* hp = m_world.tryGet<HealthComponent>(e);
    if (!hp || !hp->isAlive) return false;

    Entity building = m_build.tryBuild(m_world, m_map,
                                        static_cast<uint32_t>(hp->team),
                                        e, tileX, tileY, type, direction);
    if (!building.isValid()) {
        DZ_LOG_DEBUG("[Logic] Build failed for owner %u at tile (%d,%d)",
                     ownerID, tileX, tileY);
        return false;
    }
    if (auto* net = m_world.tryGet<NetworkComponent>(e)) {
        net->markDirty(DIRTY_INVENTORY);
    }
    return true;
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

void GameLogic::handleFlamethrowerBurst(uint32_t ownerID, float aimAngle) {
    Entity e = findOwnedEntity(ownerID);
    if (!e.isValid()) return;

    auto* hp  = m_world.tryGet<HealthComponent>(e);
    auto* xf  = m_world.tryGet<TransformComponent>(e);
    auto* cbt = m_world.tryGet<CombatComponent>(e);
    auto* inv = m_world.tryGet<InventoryComponent>(e);
    if (!hp || !hp->isAlive || !xf || !cbt || !inv) return;
    if (cbt->fireCooldown > 0.0f || cbt->isReloading) return;

    const Item& w = inv->equipped[static_cast<int>(inv->activeWeaponSlot)];
    if (!w.isValid() || w.key != "flamethrower") return;

    constexpr float PI = 3.14159265f;
    float rad  = aimAngle * (PI / 180.0f);
    float dirX =  std::sin(rad);
    float dirY = -std::cos(rad);

    for (float dist : {48.0f, 80.0f, 112.0f, 144.0f}) {
        m_fire.igniteAtWorld(xf->x + dirX * dist, xf->y + dirY * dist);
    }

    cbt->fireCooldown = 0.25f;
    cbt->emitNoise(NOISE_RUN_RADIUS, 3);
}

void GameLogic::handleMeleeAttack(uint32_t ownerID) {
    Entity e = findOwnedEntity(ownerID);
    if (!e.isValid()) return;
    auto* inv = m_world.tryGet<InventoryComponent>(e);
    if (!inv || !inv->activeWeapon().isValid() || inv->activeWeapon().category != ItemCategory::Weapon) return;
    
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
    // Determine damage and noise from equipped weapon
    float damage  = 60.0f;
    float noiseR  = NOISE_PISTOL_RADIUS;
    auto* inv = m_world.tryGet<InventoryComponent>(e);
    if (!inv) return;
    
    Item& w = inv->equipped[static_cast<int>(inv->activeWeaponSlot)];
    if (!w.isValid() || w.category != ItemCategory::Weapon) return;
    if (w.key != "pistol_9mm") return; // 현재는 권총만 사격 지원
    if (w.quantity <= 0) {
        handleReload(ownerID);
        return;
    }

    damage = 60.0f; noiseR = NOISE_PISTOL_RADIUS;

    --w.quantity; // 잔탄 1 감소
    cbt->fireCooldown = cbt->fireRate;
    cbt->emitNoise(noiseR, 3); // Loud
    
    // 탄약 변경 사항을 클라이언트에 동기화
    auto* net = m_world.tryGet<NetworkComponent>(e);
    if (net) net->markDirty(DIRTY_INVENTORY);

    if (w.quantity <= 0) {
        handleReload(ownerID);
    }

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
    auto* inv = m_world.tryGet<InventoryComponent>(e);
    if (!cbt || !inv || cbt->isReloading) return;
    
    Item& w = inv->equipped[static_cast<int>(inv->activeWeaponSlot)];
    if (!w.isValid() || w.key != "pistol_9mm") return;
    
    int magCapacity = cbt->magCapacity;
    if (w.quantity >= magCapacity) return; // 이미 만탄
    
    // 예비 탄약이 있는지 확인
    int reserve = 0;
    for (const auto& slot : inv->slots) {
        if (slot.isValid() && slot.key == "ammo_9mm") reserve += slot.quantity;
    }
    if (reserve <= 0) return; // 예비 탄약 없음

    cbt->isReloading = true;
    cbt->reloadTimer = cbt->reloadTime;
    
    auto* net = m_world.tryGet<NetworkComponent>(e);
    if (net) net->markDirty(DIRTY_HEALTH); // 상태 플래그(RELOADING) 전송용
    
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
    bool transferredAny = false;
    if (linv) {
        for (int i = 0; i < INVENTORY_GRID_SLOTS; ++i) {
            if (!linv->slots[i].isValid()) continue;
            if (inv->addItem(linv->slots[i])) {
                linv->removeItem(i);
                transferredAny = true;
            }
        }
    }

    if (transferredAny) {
        if (auto* net = m_world.tryGet<NetworkComponent>(e)) {
            net->markDirty(DIRTY_INVENTORY);
        }
    }

    bool lootEmpty = true;
    if (linv) {
        for (const auto& slot : linv->slots) {
            if (slot.isValid()) {
                lootEmpty = false;
                break;
            }
        }
    }

    if (lootEmpty) {
        m_world.destroyEntity(loot);
        DZ_LOG_INFO("[Logic] Loot %u picked up by owner %u", lootNetID, ownerID);
    } else {
        inv->recalculateGridStats();
        DZ_LOG_WARN("[Logic] Loot %u not fully picked up by owner %u; slots=%d/%d weight=%.1f/%.1f",
                    lootNetID, ownerID, inv->usedSlots, INVENTORY_GRID_SLOTS,
                    inv->currentWeight, inv->maxCarryWeight);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// handleCraftRequest — ItemData.h 레시피 테이블 기반 범용 조합 처리
// ─────────────────────────────────────────────────────────────────────────────
bool GameLogic::handleCraftRequest(uint32_t ownerID, uint8_t recipeID) {
    Entity e = findOwnedEntity(ownerID);
    if (!e.isValid()) return false;

    auto* xf  = m_world.tryGet<TransformComponent>(e);
    auto* inv = m_world.tryGet<InventoryComponent>(e);
    if (!xf || !inv) return false;

    // recipeID 유효성 검사
    if (recipeID >= CRAFT_RECIPE_COUNT) {
        DZ_LOG_WARN("[Craft] Invalid recipeID %u from owner %u", recipeID, ownerID);
        return false;
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
            return false;
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
            return false;
        }
    }

    // 인벤토리 여유 공간 확인
    if (inv->isFull()) {
        DZ_LOG_DEBUG("[Craft] %u: inventory full", ownerID);
        return false;
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
    const ItemDef def = itemDefForKey(rec.resultKey);
    Item result;
    result.itemID   = def.id;
    result.key      = rec.resultKey;
    result.category = def.category;
    result.quantity = rec.resultQty;
    result.weight   = def.weight;
    inv->addItem(result);
    if (auto* net = m_world.tryGet<NetworkComponent>(e)) {
        net->markDirty(DIRTY_INVENTORY);
    }

    DZ_LOG_INFO("[Craft] owner=%u recipe=%u → %s x%d",
                ownerID, recipeID, rec.resultKey, rec.resultQty);
    return true;
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
        --inv->slots[i].quantity;
        if (inv->slots[i].quantity <= 0) {
            inv->slots[i] = {};
        }
        inv->recalculateGridStats();
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

    auto* net = m_world.tryGet<NetworkComponent>(e);
    if (net) {
        net->markDirty(DIRTY_INVENTORY);
        net->markDirty(DIRTY_HEALTH);
    }
}

Entity GameLogic::findOwnedEntity(uint32_t ownerID) {
    for (EntityID id : m_world.alive()) {
        Entity e{id};
        auto* net = m_world.tryGet<NetworkComponent>(e);
        if (net && net->role == NetRole::LocallyOwned && net->ownerID == ownerID) return e;
    }
    return Entity{NULL_ENTITY};
}

} // namespace dz
