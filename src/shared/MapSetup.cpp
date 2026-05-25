#include "MapSetup.h"

namespace dz {

// ─────────────────────────────────────────────────────────────────────────────
// 건물 레이아웃 — 렌더러 쪽 BuildingDef에서 구조 정보만 추출한 것.
// 색상/지붕 등 렌더링 데이터는 포함하지 않는다.
// ─────────────────────────────────────────────────────────────────────────────
struct DoorEntry  { uint8_t side; float posAlong; };
struct BuildEntry { int tx, ty, tw, th; DoorEntry doors[4]; int doorCount; };

static const BuildEntry k_buildings[] = {
    // ═══ 구역 A — 주거지역 ═════════════════════════════════════════════════
    { 6,  6, 9, 7, {{1,0.35f},{1,0.70f},{2,0.50f},{0,0}}, 3 },
    {17,  6, 8, 6, {{1,0.40f},{3,0.50f},{0,0},{0,0}}, 2 },
    {27,  6, 7, 6, {{0,0.40f},{1,0.50f},{0,0},{0,0}}, 2 },
    { 5, 15,11, 8, {{1,0.30f},{1,0.65f},{2,0.40f},{3,0.60f}}, 4 },
    {18, 15, 9, 7, {{0,0.50f},{1,0.50f},{3,0.40f},{0,0}}, 3 },
    {29, 15, 7, 7, {{0,0.50f},{1,0.50f},{0,0},{0,0}}, 2 },
    { 6, 25, 8, 5, {{1,0.25f},{1,0.75f},{0,0},{0,0}}, 2 },
    {16, 25, 8, 5, {{1,0.25f},{1,0.75f},{0,0},{0,0}}, 2 },
    {26, 25, 7, 5, {{1,0.50f},{0,0},{0,0},{0,0}}, 1 },
    {30,  6, 6, 4, {{1,0.40f},{2,0.50f},{0,0},{0,0}}, 2 },
    {30, 12, 5, 4, {{1,0.50f},{0,0},{0,0},{0,0}}, 1 },

    // ═══ 구역 B — 쇼핑몰 폐허 ════════════════════════════════════════════
    {36, 36,20,13, {{0,0.25f},{0,0.75f},{1,0.25f},{1,0.75f}}, 4 },
    {36, 50,10, 6, {{1,0.40f},{3,0.50f},{0,0},{0,0}}, 2 },
    {47, 50,10, 6, {{1,0.40f},{2,0.50f},{0,0},{0,0}}, 2 },
    {36, 31, 5, 5, {{1,0.50f},{0,0},{0,0},{0,0}}, 1 },
    {42, 31, 5, 5, {{1,0.50f},{0,0},{0,0},{0,0}}, 1 },
    {48, 31, 5, 5, {{1,0.50f},{0,0},{0,0},{0,0}}, 1 },
    {57, 36, 5,10, {{0,0.50f},{1,0.50f},{0,0},{0,0}}, 2 },

    // ═══ 구역 C — 산업단지 ═══════════════════════════════════════════════
    {57, 57,15, 8, {{0,0.30f},{0,0.70f},{1,0.50f},{0,0}}, 3 },
    {57, 67,13, 6, {{1,0.35f},{1,0.70f},{0,0},{0,0}}, 2 },
    {73, 57, 8,15, {{0,0.40f},{1,0.40f},{3,0.50f},{0,0}}, 3 },
    {57, 75,12, 5, {{0,0.50f},{1,0.50f},{0,0},{0,0}}, 2 },
    {70, 67, 7, 7, {{2,0.50f},{3,0.50f},{0,0},{0,0}}, 2 },
};

static constexpr int k_buildingCount =
    static_cast<int>(sizeof(k_buildings) / sizeof(k_buildings[0]));

// ─────────────────────────────────────────────────────────────────────────────
void applyBuildingCollisions(TileMap& map) {
    for (int bi = 0; bi < k_buildingCount; ++bi) {
        const BuildEntry& b = k_buildings[bi];

        // 1. 외벽 타일 → TILE_WALL + TILE_SOLID
        for (int tx = b.tx; tx < b.tx + b.tw; ++tx) {
            for (int ty = b.ty; ty < b.ty + b.th; ++ty) {
                bool isWall = (tx == b.tx || tx == b.tx + b.tw - 1 ||
                               ty == b.ty || ty == b.ty + b.th - 1);
                if (!isWall) continue;
                if (!map.inBounds(tx, ty)) continue;
                map.at(tx, ty).type   = TILE_WALL;
                map.at(tx, ty).flags |= TILE_SOLID;
            }
        }

        // 2. 문 위치 → 통로 (SOLID 해제)
        for (int di = 0; di < b.doorCount; ++di) {
            const DoorEntry& door = b.doors[di];
            int dtx = b.tx, dty = b.ty;
            switch (door.side) {
            case 0: dtx = b.tx + static_cast<int>(door.posAlong * (b.tw-1) + 0.5f); dty = b.ty;           break;
            case 1: dtx = b.tx + static_cast<int>(door.posAlong * (b.tw-1) + 0.5f); dty = b.ty + b.th-1;  break;
            case 2: dtx = b.tx + b.tw-1; dty = b.ty + static_cast<int>(door.posAlong * (b.th-1) + 0.5f);  break;
            case 3: dtx = b.tx;          dty = b.ty + static_cast<int>(door.posAlong * (b.th-1) + 0.5f);  break;
            default: continue;
            }
            if (!map.inBounds(dtx, dty)) continue;
            map.at(dtx, dty).type   = TILE_GRASS;
            map.at(dtx, dty).flags &= ~TILE_SOLID;
        }
    }
}

} // namespace dz
