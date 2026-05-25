#pragma once
#include "shared/TileMap.h"

namespace dz {

/// 건물 외벽 타일을 SOLID로, 문 위치를 통로로 마킹.
/// 클라이언트(Game.cpp)와 서버(GameServer.cpp) 양쪽에서 호출해야 한다.
/// 이 함수가 호출되지 않으면 서버에 벽 충돌이 없어서 플레이어가
/// 벽을 통과하고 CSP 보정으로 클라이언트도 벽 안으로 끌려 들어간다.
void applyBuildingCollisions(TileMap& map);

} // namespace dz
