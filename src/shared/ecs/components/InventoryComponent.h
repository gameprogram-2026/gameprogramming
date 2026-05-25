#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace dz {

// ─────────────────────────────────────────────────────────────────────────────
// ItemCategory — matches "category" field in data/items.json
// ─────────────────────────────────────────────────────────────────────────────
enum class ItemCategory : uint8_t {
    None,
    Weapon,       ///< Melee or ranged
    Ammo,
    Consumable,   ///< Medkit, bandage, food
    Throwable,    ///< Grenade, Molotov (fire propagation source)
    BuildMaterial,///< Wood planks → barricade, metal sheet → turret base
    KeyItem,      ///< Extraction token, team beacon
    Misc,
};

// ─────────────────────────────────────────────────────────────────────────────
// Item — lightweight value type stored in slots
// Full metadata (weight, description, icon) lives in data/items.json
// and is indexed at startup via ItemRegistry.
// ─────────────────────────────────────────────────────────────────────────────
struct Item {
    uint32_t     itemID   = 0;      ///< Matches "id" in items.json
    std::string  key;               ///< Matches "key" in items.json (e.g. "molotov")
    ItemCategory category = ItemCategory::None;
    int          quantity = 1;
    float        weight   = 0.0f;   ///< Kg, loaded from items.json

    bool isValid() const noexcept { return itemID != 0; }
};

// ─────────────────────────────────────────────────────────────────────────────
// Equipment slots
// ─────────────────────────────────────────────────────────────────────────────
enum class EquipSlot : uint8_t {
    PrimaryWeapon   = 0,
    SecondaryWeapon = 1,
    Helmet          = 2,
    Vest            = 3,
    Backpack        = 4,
    SlotCount       = 5,
};

constexpr int EQUIPMENT_SLOT_COUNT = static_cast<int>(EquipSlot::SlotCount);

// ─────────────────────────────────────────────────────────────────────────────
// InventoryComponent
//
// Extraction rule: survivors keep ALL items in their inventory on extraction.
//                  On death, the entire inventory drops as world loot.
//
// Weight cap is enforced by InventorySystem on the server.
// ─────────────────────────────────────────────────────────────────────────────

constexpr int INVENTORY_GRID_SLOTS = 20;

struct InventoryComponent {
    // ── Grid inventory (backpack / pockets) ───────────────────────────────────
    std::array<Item, INVENTORY_GRID_SLOTS> slots{};
    int usedSlots = 0;

    // ── Stash (Lobby only) ────────────────────────────────────────────────────
    std::array<Item, 40> stash{};

    // ── Currency ──────────────────────────────────────────────────────────────
    int money = 0;

    // ── Equipment ─────────────────────────────────────────────────────────────
    std::array<Item, EQUIPMENT_SLOT_COUNT> equipped{};

    // ── Weight tracking ───────────────────────────────────────────────────────
    float maxCarryWeight  = 30.0f;  ///< Base + backpack bonus
    float currentWeight   = 0.0f;

    // ── Active weapon reference ───────────────────────────────────────────────
    EquipSlot activeWeaponSlot = EquipSlot::PrimaryWeapon;

    // ── Helpers ───────────────────────────────────────────────────────────────

    const Item& activeWeapon() const noexcept {
        return equipped[static_cast<int>(activeWeaponSlot)];
    }

    bool isFull() const noexcept {
        return usedSlots >= INVENTORY_GRID_SLOTS ||
               currentWeight >= maxCarryWeight;
    }

    /// Adds item to first available grid slot.  Returns false if full.
    bool addItem(const Item& item) noexcept {
        if (isFull()) return false;
        for (auto& slot : slots) {
            if (!slot.isValid()) {
                slot = item;
                currentWeight += item.weight * item.quantity;
                ++usedSlots;
                return true;
            }
        }
        return false;
    }

    /// Removes item at grid index. Returns the removed item (or invalid).
    Item removeItem(int index) noexcept {
        if (index < 0 || index >= INVENTORY_GRID_SLOTS) return {};
        Item removed = slots[index];
        if (removed.isValid()) {
            currentWeight -= removed.weight * removed.quantity;
            --usedSlots;
            slots[index] = {};
        }
        return removed;
    }

    /// Equip item from grid slot into equipment slot.
    bool equip(int gridIndex, EquipSlot slot) noexcept {
        if (gridIndex < 0 || gridIndex >= INVENTORY_GRID_SLOTS) return false;
        int si = static_cast<int>(slot);
        std::swap(slots[gridIndex], equipped[si]);
        return true;
    }
};

} // namespace dz
