add_executable(DeadZoneServer
    main.cpp
    GameServer.cpp
    GameLogic.cpp
    ZombieAI.cpp
    FireSystem.cpp
    NetworkSystem.cpp
    Database.cpp
    systems/MovementSystem.cpp
    systems/CombatSystem.cpp
    systems/NoiseSystem.cpp
    systems/BuildSystem.cpp
    systems/AllianceSystem.cpp
    systems/ExtractionSystem.cpp
)

target_include_directories(DeadZoneServer
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_SOURCE_DIR}/src"
)

target_link_libraries(DeadZoneServer
    PRIVATE
        DeadZoneShared
        MySQL::MySQL
)

if(UNIX AND NOT APPLE)
    target_link_libraries(DeadZoneServer PRIVATE pthread)
endif()

target_compile_options(DeadZoneServer PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)
