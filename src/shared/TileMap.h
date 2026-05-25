#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <cmath>

namespace dz {

constexpr int TILE_SIZE = 32;  // pixels per tile (1 tile ≈ 1 metre at this scale)

// ─────────────────────────────────────────────────────────────────────────────
// TileFlags — per-tile property bitmask
// ─────────────────────────────────────────────────────────────────────────────
enum TileFlag : uint8_t {
    TILE_SOLID      = 1 << 0,  ///< Blocks movement
    TILE_FLAMMABLE  = 1 << 1,  ///< Can be ignited by fire
    TILE_OCCUPIED   = 1 << 2,  ///< A building entity occupies this tile
    TILE_WATER      = 1 << 3,  ///< Extinguishes fire on entry
};

// ─────────────────────────────────────────────────────────────────────────────
// TileType — base tile id mapping (matches map.json "tileTypes")
// ─────────────────────────────────────────────────────────────────────────────
enum TileType : uint8_t {
    TILE_GRASS      = 0,
    TILE_ROAD       = 1,
    TILE_WALL       = 2,
    TILE_DEBRIS     = 3,   // flammable rubble
    TILE_WOOD_FLOOR = 4,   // flammable
    TILE_ASH        = 5,   // burned-out (non-flammable)
};

// ─────────────────────────────────────────────────────────────────────────────
// Tile — one cell on the map grid
// ─────────────────────────────────────────────────────────────────────────────
struct Tile {
    TileType type  = TILE_GRASS;
    uint8_t  flags = 0;
    uint32_t entityID = 0;  ///< Building entity occupying this tile (0 = none)

    bool isSolid()     const noexcept { return (flags & TILE_SOLID)     != 0; }
    bool isFlammable() const noexcept { return (flags & TILE_FLAMMABLE) != 0; }
    bool isOccupied()  const noexcept { return (flags & TILE_OCCUPIED)  != 0; }
};

// ─────────────────────────────────────────────────────────────────────────────
// TileMap — 2D grid, authoritative on both server and client
// ─────────────────────────────────────────────────────────────────────────────
class TileMap {
public:
    TileMap() = default;
    TileMap(int w, int h);

    bool loadFromJSON(const std::string& path);

    // ── Accessors ─────────────────────────────────────────────────────────────
    int width()  const noexcept { return m_w; }
    int height() const noexcept { return m_h; }
    
    struct ExtZone {
        uint8_t id;
        int tileX, tileY, w, h;
    };
    const std::vector<ExtZone>& getExtractionZones() const { return m_extractionZones; }

    struct BuildingDef {
        int x, y, w, h;
        int theme;
    };
    const std::vector<BuildingDef>& getBuildings() const { return m_buildings; }

    bool inBounds(int tx, int ty) const noexcept {
        return tx >= 0 && ty >= 0 && tx < m_w && ty < m_h;
    }

    Tile&       at(int tx, int ty)       { return m_tiles[ty * m_w + tx]; }
    const Tile& at(int tx, int ty) const { return m_tiles[ty * m_w + tx]; }

    // ── World ↔ tile coordinate conversion ───────────────────────────────────
    static int worldToTile(float world) noexcept {
        return static_cast<int>(std::floor(world / TILE_SIZE));
    }
    static float tileToWorld(int tile) noexcept {
        return static_cast<float>(tile * TILE_SIZE);
    }
    static float tileCentre(int tile) noexcept {
        return tileToWorld(tile) + TILE_SIZE * 0.5f;
    }

    // ── Queries ───────────────────────────────────────────────────────────────
    bool isSolid(int tx, int ty) const noexcept {
        return !inBounds(tx, ty) || at(tx, ty).isSolid();
    }
    bool isFlammable(int tx, int ty) const noexcept {
        return inBounds(tx, ty) && at(tx, ty).isFlammable();
    }

    /// 축 분리 충돌 해결 — 벽 슬라이딩 시 X/Y 워프 방지
    void resolveAABB(float& wx, float& wy, float hw, float hh) const;
    void resolveAxisX(float& wx, float  wy, float hw, float hh) const; ///< X축만 밀어냄
    void resolveAxisY(float  wx, float& wy, float hw, float hh) const; ///< Y축만 밀어냄

    // ── Mutations ─────────────────────────────────────────────────────────────
    void burnTile(int tx, int ty);          ///< Converts flammable tile to ash
    void setOccupied(int tx, int ty, uint32_t entityID, bool solid);
    void clearOccupied(int tx, int ty);

private:
    int              m_w = 0, m_h = 0;
    std::vector<Tile> m_tiles;
    std::vector<ExtZone> m_extractionZones;
    std::vector<BuildingDef> m_buildings;

    static uint8_t defaultFlags(TileType t) noexcept;
};

} // namespace dz
