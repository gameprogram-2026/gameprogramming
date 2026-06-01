#include "MapSetup.h"

namespace dz {

void applyBuildingCollisions(TileMap& map) {
    for (const auto& b : map.getBuildings()) {
        for (int tx = b.x; tx < b.x + b.w; ++tx) {
            for (int ty = b.y; ty < b.y + b.h; ++ty) {
                const bool isWall = (tx == b.x || tx == b.x + b.w - 1 ||
                                     ty == b.y || ty == b.y + b.h - 1);
                if (!isWall || !map.inBounds(tx, ty)) continue;

                auto& tile = map.at(tx, ty);
                tile.type = TILE_WALL;
                tile.flags |= TILE_SOLID;
            }
        }
    }
    map.initializeBuildingDoors(false);
}

} // namespace dz
