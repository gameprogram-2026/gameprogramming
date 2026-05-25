add_library(DeadZoneShared STATIC
    # ECS core
    ecs/World.cpp
    # Tilemap + 건물 충돌 설정 (클라이언트·서버 공유)
    TileMap.cpp
    MapSetup.cpp
    # Network utilities
    network/Protocol.cpp
    network/PacketBuffer.h   # header-only; listed for IDE visibility
    network/Packet.h
    # Utilities
    util/Logger.cpp
)

target_include_directories(DeadZoneShared
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"   # allows #include "shared/..."
        "${CMAKE_SOURCE_DIR}/src"       # allows #include "shared/..." from src/
)

target_link_libraries(DeadZoneShared
    PUBLIC
        ENet::ENet
        cJSON::cJSON
)

target_compile_options(DeadZoneShared PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)
