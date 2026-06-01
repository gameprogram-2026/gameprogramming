#include "MapSetup.h"

#include <vector>

namespace dz {

void applyBuildingCollisions(TileMap& map) {
    for (const auto& b : map.getBuildings()) {
        struct Opening {
            int tx;
            int ty;
            TileType type;
            uint8_t flags;
        };

        std::vector<Opening> openings;

        for (int tx = b.x; tx < b.x + b.w; ++tx) {
            for (int ty = b.y; ty < b.y + b.h; ++ty) {
                const bool isWall = (tx == b.x || tx == b.x + b.w - 1 ||
                                     ty == b.y || ty == b.y + b.h - 1);
                if (!isWall || !map.inBounds(tx, ty)) continue;

                const auto& tile = map.at(tx, ty);
                if (!tile.isSolid()) {
                    openings.push_back({tx, ty, tile.type, tile.flags});
                }
            }
        }

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

        for (const auto& opening : openings) {
            auto& tile = map.at(opening.tx, opening.ty);
            tile.type = opening.type;
            tile.flags = opening.flags & ~TILE_SOLID;
        }
    }
}

} // namespace dz
