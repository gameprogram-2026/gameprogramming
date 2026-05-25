#include "TileMap.h"
#include "util/Logger.h"
#include <cstdio>
#include <cjson/cJSON.h>

namespace dz {

TileMap::TileMap(int w, int h) : m_w(w), m_h(h) {
    m_tiles.resize(static_cast<size_t>(w * h));
}

uint8_t TileMap::defaultFlags(TileType t) noexcept {
    switch (t) {
        case TILE_GRASS:      return 0;
        case TILE_ROAD:       return 0;
        case TILE_WALL:       return TILE_SOLID;
        case TILE_DEBRIS:     return TILE_FLAMMABLE;
        case TILE_WOOD_FLOOR: return TILE_FLAMMABLE;
        case TILE_ASH:        return 0;
    }
    return 0;
}

bool TileMap::loadFromJSON(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        DZ_LOG_ERROR("TileMap: cannot open %s", path.c_str());
        return false;
    }
    std::fseek(f, 0, SEEK_END);
    long sz = std::ftell(f);
    std::rewind(f);
    std::vector<char> buf(sz + 1, 0);
    std::fread(buf.data(), 1, sz, f);
    std::fclose(f);

    cJSON* root = cJSON_Parse(buf.data());
    if (!root) { DZ_LOG_ERROR("TileMap: JSON parse error"); return false; }

    cJSON* map = cJSON_GetObjectItem(root, "map");
    if (!map)  { cJSON_Delete(root); return false; }

    m_w = cJSON_GetObjectItem(map, "width")  ? cJSON_GetObjectItem(map, "width")->valueint  : 80;
    m_h = cJSON_GetObjectItem(map, "height") ? cJSON_GetObjectItem(map, "height")->valueint : 80;
    m_tiles.assign(static_cast<size_t>(m_w * m_h), Tile{});

    // Parse layer data if present
    cJSON* layers = cJSON_GetObjectItem(map, "layers");
    if (layers) {
        cJSON* layer0 = cJSON_GetArrayItem(layers, 0);
        if (layer0) {
            cJSON* data = cJSON_GetObjectItem(layer0, "data");
            if (data && cJSON_IsArray(data)) {
                int count = cJSON_GetArraySize(data);
                for (int i = 0; i < count && i < m_w * m_h; ++i) {
                    auto tid = static_cast<TileType>(cJSON_GetArrayItem(data, i)->valueint);
                    m_tiles[i].type  = tid;
                    m_tiles[i].flags = defaultFlags(tid);
                }
            }
        }
    }
    // Parse extraction zones
    cJSON* extZones = cJSON_GetObjectItem(map, "extractionZones");
    if (extZones && cJSON_IsArray(extZones)) {
        int count = cJSON_GetArraySize(extZones);
        for (int i = 0; i < count; ++i) {
            cJSON* item = cJSON_GetArrayItem(extZones, i);
            ExtZone ez;
            ez.id = cJSON_GetObjectItem(item, "id") ? cJSON_GetObjectItem(item, "id")->valueint : i;
            ez.tileX = cJSON_GetObjectItem(item, "tileX") ? cJSON_GetObjectItem(item, "tileX")->valueint : 0;
            ez.tileY = cJSON_GetObjectItem(item, "tileY") ? cJSON_GetObjectItem(item, "tileY")->valueint : 0;
            ez.w = cJSON_GetObjectItem(item, "w") ? cJSON_GetObjectItem(item, "w")->valueint : 1;
            ez.h = cJSON_GetObjectItem(item, "h") ? cJSON_GetObjectItem(item, "h")->valueint : 1;
            m_extractionZones.push_back(ez);
        }
    }

    // Parse buildings
    cJSON* bldgs = cJSON_GetObjectItem(map, "buildings");
    if (bldgs && cJSON_IsArray(bldgs)) {
        int count = cJSON_GetArraySize(bldgs);
        for (int i = 0; i < count; ++i) {
            cJSON* item = cJSON_GetArrayItem(bldgs, i);
            BuildingDef bd;
            bd.x = cJSON_GetObjectItem(item, "x") ? cJSON_GetObjectItem(item, "x")->valueint : 0;
            bd.y = cJSON_GetObjectItem(item, "y") ? cJSON_GetObjectItem(item, "y")->valueint : 0;
            bd.w = cJSON_GetObjectItem(item, "w") ? cJSON_GetObjectItem(item, "w")->valueint : 1;
            bd.h = cJSON_GetObjectItem(item, "h") ? cJSON_GetObjectItem(item, "h")->valueint : 1;
            bd.theme = cJSON_GetObjectItem(item, "theme") ? cJSON_GetObjectItem(item, "theme")->valueint : 0;
            m_buildings.push_back(bd);
        }
    }

    // If no data, tiles default to grass (already set by assign)

    cJSON_Delete(root);
    DZ_LOG_INFO("TileMap loaded: %dx%d", m_w, m_h);
    return true;
}

// ── 축 분리 resolve — 벽 슬라이딩 버그 방지용 ────────────────────────────────
void TileMap::resolveAxisX(float& wx, float wy, float hw, float hh) const {
    constexpr float EPS = 0.001f;
    int x0 = worldToTile(wx - hw);
    int x1 = worldToTile(wx + hw - EPS);
    int y0 = worldToTile(wy - hh + EPS);   // +EPS: 수직 경계 타일 제외
    int y1 = worldToTile(wy + hh - EPS);   // -EPS: 수직 경계 타일 제외
    for (int ty = y0; ty <= y1; ++ty) {
        for (int tx = x0; tx <= x1; ++tx) {
            if (!isSolid(tx, ty)) continue;
            float tL = tileToWorld(tx), tR = tL + TILE_SIZE;
            float ol  = (wx + hw) - tL;
            float or_ = tR - (wx - hw);
            if (ol <= 0.0f || or_ <= 0.0f) continue;
            if (ol < or_) wx -= ol; else wx += or_;
        }
    }
}

void TileMap::resolveAxisY(float wx, float& wy, float hw, float hh) const {
    constexpr float EPS = 0.001f;
    int x0 = worldToTile(wx - hw + EPS);   // +EPS: 수평 경계 타일 제외
    int x1 = worldToTile(wx + hw - EPS);   // -EPS: 수평 경계 타일 제외
    int y0 = worldToTile(wy - hh);
    int y1 = worldToTile(wy + hh - EPS);
    for (int ty = y0; ty <= y1; ++ty) {
        for (int tx = x0; tx <= x1; ++tx) {
            if (!isSolid(tx, ty)) continue;
            float tT = tileToWorld(ty), tB = tT + TILE_SIZE;
            float ot  = (wy + hh) - tT;
            float ob  = tB - (wy - hh);
            if (ot <= 0.0f || ob <= 0.0f) continue;
            if (ot < ob) wy -= ot; else wy += ob;
        }
    }
}

void TileMap::resolveAABB(float& wx, float& wy, float hw, float hh) const {
    // Two-pass tile-range sweep — X axis first, then Y axis.
    //
    // Key design: half-open intervals.
    //   worldToTile uses integer truncation, so a point exactly on a tile
    //   boundary (e.g. world=32.0) maps to the NEXT tile (tile 1), even
    //   though the entity edge is flush against it without actually
    //   penetrating.  Using (edge - EDGE_EPS) as the upper bound converts
    //   the closed range into a half-open [min, max) range, which gives
    //   correct "touching but not inside" semantics without any per-axis
    //   insets that would make walls appear thicker or miss corner tiles.
    //
    //   EDGE_EPS = 0.001f  (sub-pixel; irrelevant for gameplay)
    //
    // Pass 1 resolves X, then wx is recomputed for Pass 2 so that a tile
    // the entity slid out of in X is not spuriously checked in Y.

    static constexpr float EDGE_EPS = 0.001f;

    // ── Pass 1 : push out on X axis ───────────────────────────────────────────
    {
        int x0 = worldToTile(wx - hw);
        int x1 = worldToTile(wx + hw - EDGE_EPS);
        int y0 = worldToTile(wy - hh);
        int y1 = worldToTile(wy + hh - EDGE_EPS);

        for (int ty = y0; ty <= y1; ++ty) {
            for (int tx = x0; tx <= x1; ++tx) {
                if (!isSolid(tx, ty)) continue;
                float tL = tileToWorld(tx);
                float tR = tL + TILE_SIZE;
                float ol  = (wx + hw) - tL;
                float or_ = tR - (wx - hw);
                if (ol <= 0.0f || or_ <= 0.0f) continue;
                if (ol < or_) wx -= ol;
                else          wx += or_;
            }
        }
    }

    // ── Pass 2 : push out on Y axis ───────────────────────────────────────────
    // Recompute the X tile range because wx may have changed in Pass 1.
    {
        int x0 = worldToTile(wx - hw);
        int x1 = worldToTile(wx + hw - EDGE_EPS);
        int y0 = worldToTile(wy - hh);
        int y1 = worldToTile(wy + hh - EDGE_EPS);

        for (int ty = y0; ty <= y1; ++ty) {
            for (int tx = x0; tx <= x1; ++tx) {
                if (!isSolid(tx, ty)) continue;
                float tT = tileToWorld(ty);
                float tB = tT + TILE_SIZE;
                float ot  = (wy + hh) - tT;
                float ob  = tB - (wy - hh);
                if (ot <= 0.0f || ob <= 0.0f) continue;
                if (ot < ob) wy -= ot;
                else         wy += ob;
            }
        }
    }
}

void TileMap::burnTile(int tx, int ty) {
    if (!inBounds(tx, ty)) return;
    Tile& t = at(tx, ty);
    t.type  = TILE_ASH;
    t.flags = 0;  // ash is not solid, not flammable
}

void TileMap::setOccupied(int tx, int ty, uint32_t entityID, bool solid) {
    if (!inBounds(tx, ty)) return;
    Tile& t = at(tx, ty);
    t.flags    |= TILE_OCCUPIED;
    t.entityID  = entityID;
    if (solid) t.flags |= TILE_SOLID;
}

void TileMap::clearOccupied(int tx, int ty) {
    if (!inBounds(tx, ty)) return;
    Tile& t = at(tx, ty);
    t.flags   &= ~(TILE_OCCUPIED | TILE_SOLID);
    t.entityID = 0;
}

} // namespace dz
