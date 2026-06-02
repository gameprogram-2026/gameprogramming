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
    {"nail_bat",         "못 박은 배트",        "weapon_bat.png",     "명중 시 출혈을 유발하는 근접 무기.",      ItemGrade::Enhanced, 1},
    {"fire_axe",         "소방 도끼",           "weapon_axe.png",     "문과 바리케이드를 부수는 도끼.",          ItemGrade::Enhanced, 1},
    {"pistol_9mm",      "9mm 권총",           "weapon_pistol.png",  "표준 반자동 권총. 9mm 탄환 사용.",      ItemGrade::Enhanced, 1},
    {"flamethrower",    "화염방사기",          "weapon_flame.png",   "연료 탱크와 노즐로 구성. 범위 화염 공격.",ItemGrade::Rare,     1},
    {"molotov",          "소주병 화염병",       "item_molotov.png",   "투척 후 화염을 전파합니다.",             ItemGrade::Normal,   5},
    {"ammo_9mm",         "9mm 탄환",           "ammo_9mm.png",       "권총용 탄환.",                         ItemGrade::Normal,   60},
    {"scrap_metal",      "고철",               "mat_metal.png",      "바리케이드와 포탑 제작 재료.",            ItemGrade::Normal,   20},
    {"plank",            "판자",               "mat_wood.png",       "바리케이드 제작 재료.",                  ItemGrade::Normal,   20},
    {"electronic_part",  "전자 부품",           "mat_elec.png",       "포탑 제작 재료.",                       ItemGrade::Rare,     10},
    {"oil",              "기름",               "mat_oil.png",        "포탑과 화염 무기 제작 재료.",             ItemGrade::Normal,   10},
    {"wood",             "나무",               "mat_wood.png",       "기초 제작 재료.",                       ItemGrade::Normal,   20},
    {"medkit",          "구급 상자",           "item_medkit.png",    "HP 50 회복. 출혈 상태 해제.",           ItemGrade::Enhanced, 5},
    {"bandage",         "붕대",               "item_bandage.png",   "HP 20 회복. 출혈 상태 해제.",           ItemGrade::Normal,   10},
    {"food_can",        "통조림",             "item_food.png",      "HP 10 회복. 생존 필수품.",              ItemGrade::Normal,   10},
};
static const int ITEM_META_COUNT = static_cast<int>(sizeof(ITEM_META) / sizeof(ITEM_META[0]));

// ─────────────────────────────────────────────────────────────────────────────
// 조합 레시피 테이블
// ─────────────────────────────────────────────────────────────────────────────
static const CraftingRecipe CRAFT_RECIPES[] = {
    // id, resultKey, resultQty, ingredients[], ingredientCount, requiresWorkbench, displayName
    {0,  "molotov",          1,  {{"oil",1},{"wood",1},{"",0},{"",0}},              2, false, "화염병 제작"},
    {1,  "bandage",          2,  {{"food_can",1},{"",0},{"",0},{"",0}},             1, false, "붕대 제작"},
    {2,  "medkit",           1,  {{"bandage",3},{"oil",1},{"",0},{"",0}},           2, false, "구급 상자 제작"},
    {3,  "ammo_9mm",         14, {{"scrap_metal",1},{"",0},{"",0},{"",0}},          1, true,  "9mm 탄 제작"},
    {4,  "electronic_part",  1,  {{"scrap_metal",1},{"oil",1},{"",0},{"",0}},       2, true,  "전자 부품 조립"},
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
