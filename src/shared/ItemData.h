#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// ItemData.h — 아이템 메타데이터 (서버/클라이언트 공유)
//
// 아이템 key(영문) → 표시 이름(한국어), 아이콘 경로, 설명, 조합 레시피 정의
// ─────────────────────────────────────────────────────────────────────────────
#include <cstdint>
#include <string>

namespace dz {

// 아이템 등급
enum class ItemGrade : uint8_t {
    Normal    = 0,
    Enhanced  = 1,
    Rare      = 2,
    Unique    = 3,
};

// 아이템 메타 (정적 테이블)
struct ItemMeta {
    const char* key;          // 서버 키 (영문)
    const char* displayName;  // 화면 표시 이름 (한국어)
    const char* iconPath;     // assets/sprites/items/ 상대 경로
    const char* description;  // 툴팁 설명
    ItemGrade   grade;
    float       maxStack;     // 최대 스택
};

// 레시피 재료 1종
struct RecipeIngredient {
    const char* key;
    int         qty;
};

// 조합 레시피
struct CraftingRecipe {
    uint8_t            id;
    const char*        resultKey;
    int                resultQty;
    RecipeIngredient   ingredients[4];
    int                ingredientCount;
    bool               requiresWorkbench; // true면 워크벤치 근처에서만
    const char*        displayName;       // UI에 표시할 이름
};

// ─────────────────────────────────────────────────────────────────────────────
// 아이템 메타 테이블
// ─────────────────────────────────────────────────────────────────────────────
static const ItemMeta ITEM_META[] = {
    // key                displayName         iconPath              description                         grade            maxStack
    {"scrap_pipe",       "고철 파이프",        "weapon_pipe.png",    "녹슨 파이프. 근접 공격 무기.",          ItemGrade::Normal,   1},
    {"axe",             "도끼",               "weapon_axe.png",     "나무를 자르는 도끼. 강력한 근접 무기.",   ItemGrade::Enhanced, 1},
    {"pistol_9mm",      "9mm 권총",           "weapon_pistol.png",  "표준 반자동 권총. 9mm 탄환 사용.",      ItemGrade::Enhanced, 1},
    {"flamethrower",    "화염방사기",          "weapon_flame.png",   "연료 탱크와 노즐로 구성. 범위 화염 공격.",ItemGrade::Rare,     1},
    {"9mm_ammo",        "9mm 탄환",           "ammo_9mm.png",       "권총용 탄환.",                         ItemGrade::Normal,   60},
    {"medkit",          "구급 상자",           "item_medkit.png",    "HP 50 회복. 출혈 상태 해제.",           ItemGrade::Enhanced, 5},
    {"bandage",         "붕대",               "item_bandage.png",   "HP 20 회복. 출혈 상태 해제.",           ItemGrade::Normal,   10},
    {"food_can",        "통조림",             "item_food.png",      "HP 10 회복. 생존 필수품.",              ItemGrade::Normal,   10},
    {"wood_plank",      "목재",               "mat_wood.png",       "건설 재료. 제작대 제작에 필요.",          ItemGrade::Normal,   20},
    {"metal_sheet",     "금속판",             "mat_metal.png",      "건설 재료. 포탑 제작에 필요.",            ItemGrade::Normal,   20},
    {"electronic",      "전자 부품",           "mat_elec.png",       "전자 장비 제작에 사용.",                 ItemGrade::Enhanced, 10},
    {"barricade_item",  "바리케이드",          "building_barricade.png","설치 가능한 바리케이드.",             ItemGrade::Normal,   1},
    {"turret_item",     "포탑",               "building_turret.png","자동 사격 포탑.",                       ItemGrade::Rare,     1},
    {"rag",             "헝겊",               "mat_rag.png",        "버려진 천조각. 붕대 제작 재료.",          ItemGrade::Normal,   20},
    {"alcohol",         "알코올",             "mat_alcohol.png",    "소독제. 구급 상자 제작 재료.",            ItemGrade::Normal,   10},
    {"rope",            "밧줄",               "mat_rope.png",       "바리케이드 제작 재료.",                   ItemGrade::Normal,   10},
    {"battery",         "배터리",             "mat_battery.png",    "전자 부품 제작 재료.",                    ItemGrade::Normal,   10},
    {"wire",            "전선",               "mat_wire.png",       "전자 부품 제작 재료.",                    ItemGrade::Normal,   10},
};
static const int ITEM_META_COUNT = static_cast<int>(sizeof(ITEM_META) / sizeof(ITEM_META[0]));

// ─────────────────────────────────────────────────────────────────────────────
// 조합 레시피 테이블
// ─────────────────────────────────────────────────────────────────────────────
static const CraftingRecipe CRAFT_RECIPES[] = {
    // id, resultKey, resultQty, ingredients[], ingredientCount, requiresWorkbench, displayName
    // ── 현장 제작 (워크벤치 불필요) ───────────────────────────────────────────
    {0,  "bandage",        2, {{"rag",1},{"",0},{"",0},{"",0}},         1, false, "붕대 제작"},
    {1,  "medkit",         1, {{"bandage",3},{"alcohol",1},{"",0},{"",0}}, 2, false, "구급 상자 제작"},

    // ── 워크벤치 전용 레시피 ──────────────────────────────────────────────────
    {2,  "barricade_item", 1, {{"wood_plank",2},{"rope",1},{"",0},{"",0}},  2, true,  "바리케이드 제작"},
    {3,  "turret_item",    1, {{"metal_sheet",2},{"electronic",1},{"battery",1}}, 3, true, "포탑 제작"},
    {4,  "electronic",     1, {{"wire",2},{"battery",1},{"",0},{"",0}},     2, true,  "전자 부품 조립"},
    {5,  "wood_plank",     3, {{"rope",1},{"",0},{"",0},{"",0}},            1, false, "목재 가공"},  // 밧줄→목재로
};
static const int CRAFT_RECIPE_COUNT = static_cast<int>(sizeof(CRAFT_RECIPES) / sizeof(CRAFT_RECIPES[0]));

// ── 헬퍼 함수 ─────────────────────────────────────────────────────────────────
inline const ItemMeta* findItemMeta(const std::string& key) {
    for (int i = 0; i < ITEM_META_COUNT; ++i)
        if (key == ITEM_META[i].key) return &ITEM_META[i];
    return nullptr;
}

inline const char* getDisplayName(const std::string& key) {
    const ItemMeta* m = findItemMeta(key);
    return m ? m->displayName : key.c_str();
}

inline const char* getIconPath(const std::string& key) {
    const ItemMeta* m = findItemMeta(key);
    return m ? m->iconPath : nullptr;
}

} // namespace dz
